// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <lz4.h>
#include <nce.h>
#include <os.h>
#include <kernel/types/KProcess.h>
#include <boost/regex/v5/regex.hpp>
#include "nso.h"

namespace skyline::loader {
    namespace {
        constexpr size_t NsoHeaderSize{0x100}; //!< Patch offsets are relative to the start of the NSO file, which includes the header in front of the .text segment

        std::string ToHexString(const std::array<u64, 4> &id) {
            std::string out;
            for (u64 part : id)
                for (size_t i{}; i < sizeof(u64); i++)
                    out += fmt::format("{:02X}", static_cast<u8>(part >> (i * 8)));
            return out;
        }

        std::string ToUpper(std::string string) {
            std::transform(string.begin(), string.end(), string.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            return string;
        }

        std::string Trim(const std::string &string) {
            size_t start{string.find_first_not_of(" \t\r\n")};
            if (start == std::string::npos)
                return {};
            return string.substr(start, string.find_last_not_of(" \t\r\n") - start + 1);
        }

        /**
         * @brief Writes patch bytes to an offset from the start of the module (the start of .text), the write is dropped if it isn't fully inside one segment
         */
        bool WritePatch(Executable &executable, size_t moduleOffset, const std::vector<u8> &bytes) {
            for (auto *segment : {&executable.text, &executable.ro, &executable.data}) {
                if (moduleOffset >= segment->offset && moduleOffset + bytes.size() <= segment->offset + segment->contents.size()) {
                    std::memcpy(segment->contents.data() + (moduleOffset - segment->offset), bytes.data(), bytes.size());
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Applies an IPSwitch text patch (.pchtxt) if it targets the given build ID
         * @url https://github.com/3096/ipswitch
         * @return The amount of writes that were applied
         */
        size_t ApplyPchtxt(const std::string &text, const std::string &moduleIdHex, Executable &executable) {
            std::istringstream stream{text};
            std::string rawLine;
            bool targetsModule{};
            bool enabled{};
            i64 offsetShift{};
            size_t applied{};

            while (std::getline(stream, rawLine)) {
                std::string line{Trim(rawLine)};
                if (line.empty() || line.rfind("//", 0) == 0 || line[0] == '#')
                    continue;

                if (line[0] == '@') {
                    if (line.rfind("@nsobid-", 0) == 0) {
                        std::string id{ToUpper(Trim(line.substr(8)))};
                        targetsModule = !id.empty() && moduleIdHex.rfind(id, 0) == 0;
                    } else if (line.rfind("@flag", 0) == 0) {
                        std::istringstream flag{line};
                        std::string at, name, value;
                        flag >> at >> name >> value;
                        if (name == "offset_shift" && !value.empty())
                            offsetShift = static_cast<i64>(std::strtoll(value.c_str(), nullptr, 0));
                    } else if (line == "@enabled") {
                        enabled = true;
                    } else if (line == "@disabled") {
                        enabled = false;
                    } else if (line == "@stop") {
                        break; // Everything after this is ignored
                    }
                    continue;
                }

                if (!targetsModule || !enabled)
                    continue;

                std::istringstream entry{line};
                std::string offsetString, valueString;
                entry >> offsetString;
                std::getline(entry, valueString);
                valueString = Trim(valueString);
                if (offsetString.empty() || valueString.empty())
                    continue;

                std::vector<u8> bytes;
                if (valueString.front() == '"') {
                    size_t end{valueString.find_last_of('"')};
                    if (end == 0)
                        continue;
                    std::string str{valueString.substr(1, end - 1)};
                    bytes.assign(str.begin(), str.end());
                } else {
                    if (valueString.size() % 2 != 0)
                        continue;
                    bool valid{true};
                    for (size_t i{}; i < valueString.size() && valid; i += 2) {
                        char byteString[3]{valueString[i], valueString[i + 1], 0};
                        char *end{};
                        unsigned long value{std::strtoul(byteString, &end, 16)};
                        valid = *end == '\0';
                        bytes.push_back(static_cast<u8>(value));
                    }
                    if (!valid)
                        continue;
                }

                // The offsets are written with the NSO header included (like IPS patches), so the module offset has the header size taken off
                i64 moduleOffset{static_cast<i64>(std::strtoull(offsetString.c_str(), nullptr, 16)) + offsetShift - static_cast<i64>(NsoHeaderSize)};
                if (moduleOffset < 0)
                    continue;
                if (WritePatch(executable, static_cast<size_t>(moduleOffset), bytes))
                    applied++;
                else
                    LOGW("Patch at 0x{:X} is outside of the module's segments, ignoring it", moduleOffset);
            }

            return applied;
        }

        /**
         * @brief Applies an IPS/IPS32 patch (the file name has to be the build ID of the module it targets)
         * @return The amount of writes that were applied
         */
        size_t ApplyIps(const std::vector<u8> &data, Executable &executable) {
            bool ips32{data.size() >= 5 && std::memcmp(data.data(), "IPS32", 5) == 0};
            if (!ips32 && !(data.size() >= 5 && std::memcmp(data.data(), "PATCH", 5) == 0))
                return 0;

            const size_t offsetSize{ips32 ? 4UL : 3UL};
            size_t position{5};
            size_t applied{};

            while (position + offsetSize <= data.size()) {
                if (position + 3 <= data.size() && std::memcmp(data.data() + position, "EOF", 3) == 0)
                    break;
                if (position + 4 <= data.size() && std::memcmp(data.data() + position, "EEOF", 4) == 0)
                    break;
                if (position + offsetSize + 2 > data.size())
                    break;

                size_t offset{};
                for (size_t i{}; i < offsetSize; i++)
                    offset = (offset << 8) | data[position + i];
                position += offsetSize;

                size_t size{static_cast<size_t>((data[position] << 8) | data[position + 1])};
                position += 2;

                std::vector<u8> bytes;
                if (size == 0) {
                    // RLE record: a 16-bit repeat count followed by the byte to repeat
                    if (position + 3 > data.size())
                        break;
                    size_t count{static_cast<size_t>((data[position] << 8) | data[position + 1])};
                    bytes.assign(count, data[position + 2]);
                    position += 3;
                } else {
                    if (position + size > data.size())
                        break;
                    bytes.assign(data.begin() + static_cast<std::ptrdiff_t>(position), data.begin() + static_cast<std::ptrdiff_t>(position + size));
                    position += size;
                }

                if (offset < NsoHeaderSize)
                    continue;
                if (WritePatch(executable, offset - NsoHeaderSize, bytes))
                    applied++;
            }

            return applied;
        }

        /**
         * @brief Applies every .pchtxt and .ips patch in the patches directory that targets this module, this allows fixing games and installing translations that are distributed as code patches
         */
        void ApplyExecutablePatches(const DeviceState &state, const std::array<u64, 4> &buildId, Executable &executable, const std::string &name) {
            std::error_code error;
            std::filesystem::path patchesDirectory{state.os->publicAppFilesPath + "/patches/"};
            if (!std::filesystem::is_directory(patchesDirectory, error))
                return;

            std::string moduleIdHex{ToHexString(buildId)};
            LOGI("Build ID of '{}': {}", name, moduleIdHex.substr(0, 32)); // This is what patches target (@nsobid- in .pchtxt files, the file name of .ips files)
            for (auto it{std::filesystem::recursive_directory_iterator(patchesDirectory, error)}; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                if (!it->is_regular_file(error))
                    continue;

                std::string extension{ToUpper(it->path().extension().string())};
                try {
                    size_t applied{};
                    if (extension == ".PCHTXT" || extension == ".IPS") {
                        std::ifstream file{it->path(), std::ios::binary};
                        std::vector<u8> contents{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

                        if (extension == ".PCHTXT") {
                            applied = ApplyPchtxt(std::string(contents.begin(), contents.end()), moduleIdHex, executable);
                        } else {
                            std::string stem{ToUpper(it->path().stem().string())};
                            if (!stem.empty() && moduleIdHex.rfind(stem, 0) == 0)
                                applied = ApplyIps(contents, executable);
                        }
                    }

                    if (applied)
                        LOGI("Applied {} write(s) from patch '{}' to '{}'", applied, it->path().filename().string(), name);
                } catch (const std::exception &e) {
                    LOGW("Failed to apply patch '{}': {}", it->path().filename().string(), e.what());
                }
            }
        }
    }
    NsoLoader::NsoLoader(std::shared_ptr<vfs::Backing> pBacking) : backing(std::move(pBacking)) {
        u32 magic{backing->Read<u32>()};

        if (magic != util::MakeMagic<u32>("NSO0"))
            throw exception("Invalid NSO magic! 0x{0:X}", magic);
    }

    std::vector<u8> NsoLoader::GetSegment(const std::shared_ptr<vfs::Backing> &backing, const NsoSegmentHeader &segment, u32 compressedSize) {
        std::vector<u8> outputBuffer(segment.decompressedSize);

        if (compressedSize) {
            std::vector<u8> compressedBuffer(compressedSize);
            backing->Read(compressedBuffer, segment.fileOffset);

            LZ4_decompress_safe(reinterpret_cast<char *>(compressedBuffer.data()), reinterpret_cast<char *>(outputBuffer.data()), static_cast<int>(compressedSize), static_cast<int>(segment.decompressedSize));
        } else {
            backing->Read(outputBuffer, segment.fileOffset);
        }

        return outputBuffer;
    }

    Loader::ExecutableLoadInfo NsoLoader::LoadNso(Loader *loader, const std::shared_ptr<vfs::Backing> &backing, const std::shared_ptr<kernel::type::KProcess> &process, const DeviceState &state, size_t offset, const std::string &name, bool dynamicallyLinked) {
        auto header{backing->Read<NsoHeader>()};

        if (header.magic != util::MakeMagic<u32>("NSO0"))
            throw exception("Invalid NSO magic! 0x{0:X}", header.magic);

        Executable executable{};

        executable.text.contents = GetSegment(backing, header.text, header.flags.textCompressed ? header.textCompressedSize : 0);
        executable.text.contents.resize(util::AlignUp(executable.text.contents.size(), constant::PageSize));
        executable.text.offset = header.text.memoryOffset;

        executable.ro.contents = GetSegment(backing, header.ro, header.flags.roCompressed ? header.roCompressedSize : 0);
        executable.ro.contents.resize(util::AlignUp(executable.ro.contents.size(), constant::PageSize));
        executable.ro.offset = header.ro.memoryOffset;

        executable.data.contents = GetSegment(backing, header.data, header.flags.dataCompressed ? header.dataCompressedSize : 0);
        executable.data.offset = header.data.memoryOffset;

        // Data and BSS are aligned together
        executable.bssSize = util::AlignUp(executable.data.contents.size() + header.bssSize, constant::PageSize) - executable.data.contents.size();

        if (header.dynsym.offset + header.dynsym.size <= header.ro.decompressedSize && header.dynstr.offset + header.dynstr.size <= header.ro.decompressedSize) {
            executable.dynsym = {header.dynsym.offset, header.dynsym.size};
            executable.dynstr = {header.dynstr.offset, header.dynstr.size};
        }

        ApplyExecutablePatches(state, header.buildId, executable, name);

        PrintRoContentsInfo(executable.ro.contents);

        return loader->LoadExecutable(process, state, executable, offset, name, dynamicallyLinked);
    }

    void *NsoLoader::LoadProcessData(const std::shared_ptr<kernel::type::KProcess> &process, const DeviceState &state) {
        state.process->memory.InitializeVmm(memory::AddressSpaceType::AddressSpace39Bit);
        auto loadInfo{LoadNso(this, backing, process, state)};
        state.process->memory.InitializeRegions(span<u8>{loadInfo.base, loadInfo.size});
        return loadInfo.entry;
    }

    void NsoLoader::PrintRoContentsInfo(const std::vector<u8> &contents) {
        const boost::regex moduleRegex(R"([a-z]:[\\/][ -~]{5,}\.nss)", boost::regex::icase);
        const boost::regex fsSdkRegex("sdk_version: ([0-9.]*)");
        const boost::regex sdkMwRegex("SDK MW[ -~]*");

        std::string contentsRaw(contents.begin(), contents.end());
        std::string modulePath;
        if (memcmp(&contents[0], "\x00\x00\x00\x00", 4) == 0) {
            i32 length;
            std::memcpy(&length, &contents[4], sizeof(i32));

            if (length > 0)
                modulePath = reinterpret_cast<const char *>(&contents[4 + sizeof(i32)]);
        }

        if (modulePath.empty()) {
            boost::smatch moduleMatch;
            if (boost::regex_search(contentsRaw, moduleMatch, moduleRegex))
                modulePath = moduleMatch.str();
        }

        LOGI("Module Path: {}", modulePath);

        boost::smatch fsSdkMatch;
        if (boost::regex_search(contentsRaw, fsSdkMatch, fsSdkRegex))
            LOGI("SDK Version: {}", fsSdkMatch[1].str());

        boost::sregex_iterator sdkMwMatchesBegin(contentsRaw.begin(), contentsRaw.end(), sdkMwRegex);
        boost::sregex_iterator sdkMwMatchesEnd;

        if (sdkMwMatchesBegin != sdkMwMatchesEnd) {
            std::string libContent;

            while (sdkMwMatchesBegin != sdkMwMatchesEnd) {
                libContent += sdkMwMatchesBegin->str() + "\n";
                sdkMwMatchesBegin++;
            }

            LOGI("SDK Libraries: {}", libContent);
        }
    }
}
