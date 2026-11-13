// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <fstream>
#include <os.h>
#include "pipeline_cache_manager.h"

namespace skyline::gpu {
    struct PipelineCacheFileHeader {
        static constexpr u32 Magic{util::MakeMagic<u32>("PCHE")}; //!< The magic value used to identify a pipeline cache file
        static constexpr u32 Version{3}; //!< The version of the pipeline cache file format, MUST be incremented for any format changes

        u32 magic{Magic};
        u32 version{Version};
        u32 count{0}; //!< The total number of pipeline cache bundles in the file

        auto operator<=>(const PipelineCacheFileHeader &) const = default;

        /**
         * @brief Checks if the header is valid
         */
        bool IsValid() {
            return magic == Magic && version == Version;
        }
    };

    void PipelineCacheManager::Run() {
        std::ofstream stream{stagingPath, std::ios::binary | std::ios::trunc};
        PipelineCacheFileHeader header{};
        stream.write(reinterpret_cast<const char *>(&header), sizeof(PipelineCacheFileHeader));

        while (true) {
            std::unique_lock lock(writeMutex);
            if (writeQueue.empty())
                stream.flush();

            writeCondition.wait(lock, [this] { return !writeQueue.empty(); });
            auto bundle{std::move(writeQueue.front())};
            writeQueue.pop();
            lock.unlock();

            bundle->Serialise(stream);

            header.count++;
            // Rewrite the header with the updated count
            auto savedPosition{stream.tellp()};
            stream.seekp(0, std::ios_base::beg);
            stream.write(reinterpret_cast<const char *>(&header), sizeof(PipelineCacheFileHeader));
            stream.seekp(savedPosition);
            stream.flush(); // The app can be killed at any moment (the Android low memory killer does that to games that use a lot of RAM), whatever is still in the stream buffer would be lost with all the pipelines compiled in that session

            if (header.count == 1 || header.count % 250 == 0)
                LOGI("Pipeline cache: {} pipeline(s) written to '{}' this session", header.count, stagingPath);
            if (stream.fail()) {
                LOGE("Pipeline cache: writing to '{}' failed, new pipelines won't be saved", stagingPath);
                return;
            }
        }
    }

    bool PipelineCacheManager::ValidateHeader(std::ifstream &stream) {
        if (stream.fail())
            return false;

        PipelineCacheFileHeader header{};
        stream.read(reinterpret_cast<char *>(&header), sizeof(PipelineCacheFileHeader));
        return header.IsValid();
    }

    void PipelineCacheManager::MergeStaging() {
        std::ifstream stagingStream{stagingPath, std::ios::binary};
        if (stagingStream.fail())
            return; // If the staging file doesn't exist then there's nothing to merge

        PipelineCacheFileHeader stagingHeader{};
        stagingStream.read(reinterpret_cast<char *>(&stagingHeader), sizeof(PipelineCacheFileHeader));
        if (!stagingHeader.IsValid()) {
            LOGW("Discarding invalid pipeline cache staging file");
            return;
        }

        std::fstream mainStream{mainPath, std::ios::binary | std::ios::in | std::ios::out};
        PipelineCacheFileHeader mainHeader{};
        mainStream.seekg(0, std::ios_base::beg);
        mainStream.read(reinterpret_cast<char *>(&mainHeader), sizeof(PipelineCacheFileHeader));

        // Update the main header with the new count
        mainHeader.count += stagingHeader.count;
        mainStream.seekp(0, std::ios_base::beg);
        mainStream.write(reinterpret_cast<const char *>(&mainHeader), sizeof(PipelineCacheFileHeader));

        mainStream.seekp(0, std::ios::end);
        mainStream << stagingStream.rdbuf();
        LOGI("Pipeline cache: merged {} pipeline(s) from the previous session into '{}' (now {} in total)", stagingHeader.count, mainPath, mainHeader.count);
    }

    PipelineCacheManager::PipelineCacheManager(const DeviceState &state, const std::string &path)
        : stagingPath{path + ".staging"}, mainPath{path} {
        bool didExist{std::filesystem::exists(mainPath)};
        if (didExist) { // If the main file exists then we need to validate it
            std::ifstream mainStream{mainPath, std::ios::binary};
            if (!ValidateHeader(mainStream)) { // Force a recreation of the file if it's invalid
                LOGW("Discarding invalid pipeline cache main file");
                std::filesystem::remove(mainPath);
                didExist = false;
            }
        }

        if (!didExist) { // If the main file didn't exist we need to write the header
            std::filesystem::create_directories(std::filesystem::path{mainPath}.parent_path());
            std::ofstream mainStream{mainPath, std::ios::binary | std::ios::app};
            PipelineCacheFileHeader header{};
            mainStream.write(reinterpret_cast<const char *>(&header), sizeof(PipelineCacheFileHeader));
        }

        // Merge any staging changes into the main file before starting the writer thread
        std::error_code ec;
        auto stagingSize{std::filesystem::exists(stagingPath, ec) ? std::filesystem::file_size(stagingPath, ec) : 0};
        LOGI("Pipeline cache: main file '{}' ({}), staging file {} bytes", mainPath, didExist ? "existed" : "created now", stagingSize);
        MergeStaging();
        writerThread = std::thread(&PipelineCacheManager::Run, this);
    }

    void PipelineCacheManager::QueueWrite(std::unique_ptr<interconnect::PipelineStateBundle> bundle) {
        std::scoped_lock lock{writeMutex};
        writeQueue.emplace(std::move(bundle));
        writeCondition.notify_one();
    }

    std::pair<std::ifstream, u32> PipelineCacheManager::OpenReadStream() {
        auto mainStream{std::ifstream{mainPath, std::ios::binary}};
        if (mainStream.fail())
            throw exception("Pipeline cache main file missing at runtime!");

        PipelineCacheFileHeader header{};
        mainStream.read(reinterpret_cast<char *>(&header), sizeof(PipelineCacheFileHeader));
        if (!header.IsValid())
            throw exception("Pipeline cache main file corrupted at runtime!");

        return {std::move(mainStream), header.count};
    }

    void PipelineCacheManager::InvalidateAllAfter(u64 offset) {
        std::filesystem::resize_file(mainPath, offset);
    }
}
