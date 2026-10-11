// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <algorithm>
#include <filesystem>
#include <set>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include "rom_filesystem.h"
#include "layered_romfs.h"

namespace skyline::vfs {
    namespace {
        constexpr u32 Empty{0xFFFFFFFF};
        constexpr size_t MaxOpenFiles{64};

        struct BuildDirectory {
            std::string name; //!< Only the last component
            u32 parent; //!< The index of the parent directory
            std::vector<u32> directories; //!< Indices of child directories
            std::vector<std::string> files; //!< Full paths of the child files
            u32 offset{}; //!< The offset in the directory metadata table
        };

        u32 CalculateHash(u32 parent, std::string_view name) {
            u32 hash{parent ^ 123456789U};
            for (char c : name) {
                hash = (hash >> 5) | (hash << 27);
                hash ^= static_cast<u8>(c);
            }
            return hash;
        }

        u32 GetBucketCount(size_t count) {
            if (count < 3)
                return 3;
            if (count < 19)
                return static_cast<u32>(count | 1);

            u32 buckets{static_cast<u32>(count)};
            while (buckets % 2 == 0 || buckets % 3 == 0 || buckets % 5 == 0 || buckets % 7 == 0 || buckets % 11 == 0 || buckets % 13 == 0 || buckets % 17 == 0)
                buckets++;
            return buckets;
        }

        size_t AlignUp(size_t value, size_t alignment) {
            return (value + alignment - 1) & ~(alignment - 1);
        }

        template<typename T>
        void Put(std::vector<u8> &buffer, size_t offset, T value) {
            std::memcpy(buffer.data() + offset, &value, sizeof(T));
        }
    }

    LayeredRomFsBacking::LayeredRomFsBacking(std::shared_ptr<Backing> pBase, const std::map<std::string, std::string> &hostFiles) : Backing({true, false, false}), base(std::move(pBase)) {
        // Gather the files, the ones from the host replace the ones from the original image
        std::map<std::string, FileRegion> entries;
        if (base) {
            RomFileSystem baseFileSystem(base);
            for (const auto &[path, entry] : baseFileSystem.fileMap)
                entries[path] = FileRegion{0, entry.size, false, baseFileSystem.header.dataOffset + entry.offset, {}};
        }
        for (const auto &[path, hostPath] : hostFiles) {
            std::error_code error;
            auto fileSize{std::filesystem::file_size(hostPath, error)};
            if (!error)
                entries[path] = FileRegion{0, fileSize, true, 0, hostPath};
        }

        // Build the directory tree (the directories are sorted so a parent is always before its children)
        std::set<std::string> directoryPaths{""};
        for (const auto &entry : entries) {
            std::string path{entry.first};
            for (size_t slash{path.find('/')}; slash != std::string::npos; slash = path.find('/', slash + 1))
                directoryPaths.insert(path.substr(0, slash));
        }

        std::vector<BuildDirectory> directories;
        std::map<std::string, u32> directoryIndices;
        for (const auto &path : directoryPaths) {
            size_t slash{path.rfind('/')};
            std::string parentPath{slash == std::string::npos ? "" : path.substr(0, slash)};
            std::string name{slash == std::string::npos ? path : path.substr(slash + 1)};
            u32 parent{path.empty() ? 0 : directoryIndices.at(parentPath)};
            directoryIndices[path] = static_cast<u32>(directories.size());
            directories.push_back({name, parent, {}, {}, 0});
            if (!path.empty())
                directories[parent].directories.push_back(static_cast<u32>(directories.size() - 1));
        }
        for (const auto &entry : entries) {
            size_t slash{entry.first.rfind('/')};
            std::string parentPath{slash == std::string::npos ? "" : entry.first.substr(0, slash)};
            directories[directoryIndices.at(parentPath)].files.push_back(entry.first);
        }

        // Lay out the metadata tables
        size_t directoryTableSize{};
        for (auto &directory : directories) {
            directory.offset = static_cast<u32>(directoryTableSize);
            directoryTableSize += 24 + AlignUp(directory.name.size(), 4);
        }

        std::map<std::string, u32> fileOffsets;
        size_t fileTableSize{};
        for (const auto &entry : entries) {
            size_t slash{entry.first.rfind('/')};
            std::string name{slash == std::string::npos ? entry.first : entry.first.substr(slash + 1)};
            fileOffsets[entry.first] = static_cast<u32>(fileTableSize);
            fileTableSize += 32 + AlignUp(name.size(), 4);
        }

        // The file data is placed one file after another
        u64 dataSize{};
        for (auto &[path, region] : entries) {
            region.offset = dataSize;
            dataSize = AlignUp(dataSize + region.size, 0x10);
        }

        u32 directoryBuckets{GetBucketCount(directories.size())};
        u32 fileBuckets{GetBucketCount(entries.size())};
        size_t directoryHashOffset{0x50};
        size_t directoryMetaOffset{directoryHashOffset + directoryBuckets * 4};
        size_t fileHashOffset{directoryMetaOffset + directoryTableSize};
        size_t fileMetaOffset{fileHashOffset + fileBuckets * 4};
        dataOffset = AlignUp(fileMetaOffset + fileTableSize, 0x10);

        metadata.assign(dataOffset, 0);
        Put<u64>(metadata, 0x00, 0x50);
        Put<u64>(metadata, 0x08, directoryHashOffset);
        Put<u64>(metadata, 0x10, directoryBuckets * 4UL);
        Put<u64>(metadata, 0x18, directoryMetaOffset);
        Put<u64>(metadata, 0x20, directoryTableSize);
        Put<u64>(metadata, 0x28, fileHashOffset);
        Put<u64>(metadata, 0x30, fileBuckets * 4UL);
        Put<u64>(metadata, 0x38, fileMetaOffset);
        Put<u64>(metadata, 0x40, fileTableSize);
        Put<u64>(metadata, 0x48, dataOffset);

        for (size_t i{}; i < directoryBuckets; i++)
            Put<u32>(metadata, directoryHashOffset + i * 4, Empty);
        for (size_t i{}; i < fileBuckets; i++)
            Put<u32>(metadata, fileHashOffset + i * 4, Empty);

        // Directory entries
        for (u32 index{}; index < directories.size(); index++) {
            auto &directory{directories[index]};
            size_t entryOffset{directoryMetaOffset + directory.offset};
            u32 parentOffset{directories[directory.parent].offset};

            // The siblings are the other child directories of the parent, in order
            u32 sibling{Empty};
            if (index != 0) {
                const auto &siblings{directories[directory.parent].directories};
                auto position{std::find(siblings.begin(), siblings.end(), index)};
                if (position + 1 != siblings.end())
                    sibling = directories[*(position + 1)].offset;
            }

            // The hash chain
            u32 bucket{CalculateHash(parentOffset, directory.name) % directoryBuckets};
            size_t bucketOffset{directoryHashOffset + bucket * 4UL};
            u32 hashSibling{};
            std::memcpy(&hashSibling, metadata.data() + bucketOffset, sizeof(u32));
            Put<u32>(metadata, bucketOffset, directory.offset);

            Put<u32>(metadata, entryOffset + 0x00, index == 0 ? 0 : parentOffset);
            Put<u32>(metadata, entryOffset + 0x04, sibling);
            Put<u32>(metadata, entryOffset + 0x08, directory.directories.empty() ? Empty : directories[directory.directories.front()].offset);
            Put<u32>(metadata, entryOffset + 0x0C, directory.files.empty() ? Empty : fileOffsets.at(directory.files.front()));
            Put<u32>(metadata, entryOffset + 0x10, hashSibling);
            Put<u32>(metadata, entryOffset + 0x14, static_cast<u32>(directory.name.size()));
            std::memcpy(metadata.data() + entryOffset + 24, directory.name.data(), directory.name.size());
        }

        // File entries
        for (auto &directory : directories) {
            for (size_t i{}; i < directory.files.size(); i++) {
                const auto &path{directory.files[i]};
                const auto &region{entries.at(path)};
                size_t slash{path.rfind('/')};
                std::string name{slash == std::string::npos ? path : path.substr(slash + 1)};
                size_t entryOffset{fileMetaOffset + fileOffsets.at(path)};

                u32 bucket{CalculateHash(directory.offset, name) % fileBuckets};
                size_t bucketOffset{fileHashOffset + bucket * 4UL};
                u32 hashSibling{};
                std::memcpy(&hashSibling, metadata.data() + bucketOffset, sizeof(u32));
                Put<u32>(metadata, bucketOffset, fileOffsets.at(path));

                Put<u32>(metadata, entryOffset + 0x00, directory.offset);
                Put<u32>(metadata, entryOffset + 0x04, i + 1 < directory.files.size() ? fileOffsets.at(directory.files[i + 1]) : Empty);
                Put<u64>(metadata, entryOffset + 0x08, region.offset);
                Put<u64>(metadata, entryOffset + 0x10, region.size);
                Put<u32>(metadata, entryOffset + 0x18, hashSibling);
                Put<u32>(metadata, entryOffset + 0x1C, static_cast<u32>(name.size()));
                std::memcpy(metadata.data() + entryOffset + 32, name.data(), name.size());
            }
        }

        for (const auto &[path, region] : entries)
            files.push_back(region);
        std::sort(files.begin(), files.end(), [](const FileRegion &a, const FileRegion &b) { return a.offset < b.offset; });

        size = dataOffset + dataSize;
    }

    LayeredRomFsBacking::~LayeredRomFsBacking() {
        for (const auto &[path, fd] : openFiles)
            close(fd);
    }

    size_t LayeredRomFsBacking::ReadHostFile(const std::string &path, u8 *output, size_t length, size_t offset) {
        std::scoped_lock lock{fdMutex};

        auto it{openFiles.find(path)};
        if (it == openFiles.end()) {
            if (openFiles.size() >= MaxOpenFiles) {
                for (const auto &[openPath, openFd] : openFiles)
                    close(openFd);
                openFiles.clear();
            }

            int fd{open(path.c_str(), O_RDONLY | O_CLOEXEC)};
            if (fd == -1)
                return 0;
            it = openFiles.emplace(path, fd).first;
        }

        ssize_t result{pread(it->second, output, length, static_cast<off_t>(offset))};
        return result < 0 ? 0 : static_cast<size_t>(result);
    }

    size_t LayeredRomFsBacking::ReadImpl(span<u8> output, size_t offset) {
        size_t done{};
        while (done < output.size() && offset + done < size) {
            size_t position{offset + done};
            size_t remaining{std::min<size_t>(output.size() - done, size - position)};

            if (position < dataOffset) {
                size_t length{std::min<size_t>(remaining, dataOffset - position)};
                std::memcpy(output.data() + done, metadata.data() + position, length);
                done += length;
                continue;
            }

            u64 relative{position - dataOffset};
            // Find the last file that starts at or before this position
            auto it{std::upper_bound(files.begin(), files.end(), relative, [](u64 value, const FileRegion &region) { return value < region.offset; })};
            if (it != files.begin() && relative < (it - 1)->offset + (it - 1)->size) {
                const auto &region{*(it - 1)};
                size_t inFile{static_cast<size_t>(relative - region.offset)};
                size_t length{std::min<size_t>(remaining, region.size - inFile)};

                size_t read{};
                if (region.fromHost)
                    read = ReadHostFile(region.hostPath, output.data() + done, length, inFile);
                else
                    read = base->ReadUnchecked(span<u8>(output.data() + done, length), region.baseOffset + inFile);

                if (read < length)
                    std::memset(output.data() + done + read, 0, length - read); // A file that shrank on the host reads as zeros
                done += length;
            } else {
                // Padding between files
                u64 nextStart{it != files.end() ? it->offset : size - dataOffset};
                size_t length{std::min<size_t>(remaining, static_cast<size_t>(nextStart - relative))};
                std::memset(output.data() + done, 0, length);
                done += length;
            }
        }
        return done;
    }

    std::shared_ptr<Backing> LayeredRomFsBacking::Create(const std::shared_ptr<Backing> &base, const std::string &titleModsRoot) {
        std::error_code error;
        if (!base || !std::filesystem::is_directory(titleModsRoot, error))
            return base;

        // Every mod is a directory with a romfs directory inside it, the mods are applied in alphabetical order so the last one wins when two replace the same file
        std::vector<std::filesystem::path> modDirectories;
        for (auto it{std::filesystem::directory_iterator(titleModsRoot, error)}; !error && it != std::filesystem::directory_iterator(); it.increment(error))
            if (it->is_directory(error) && std::filesystem::is_directory(it->path() / "romfs", error))
                modDirectories.push_back(it->path());
        std::sort(modDirectories.begin(), modDirectories.end());

        std::map<std::string, std::string> hostFiles;
        for (const auto &modDirectory : modDirectories) {
            auto romfsDirectory{modDirectory / "romfs"};
            for (auto it{std::filesystem::recursive_directory_iterator(romfsDirectory, error)}; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                if (!it->is_regular_file(error))
                    continue;
                hostFiles[std::filesystem::relative(it->path(), romfsDirectory, error).generic_string()] = it->path().string();
            }
            LOGI("Using RomFS mod '{}'", modDirectory.filename().string());
        }

        if (hostFiles.empty())
            return base;

        LOGI("Applying {} RomFS mod file(s)", hostFiles.size());
        return std::make_shared<LayeredRomFsBacking>(base, hostFiles);
    }
}
