// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <map>
#include <mutex>
#include <unordered_map>
#include "backing.h"

namespace skyline::vfs {
    /**
     * @brief A virtual RomFS image that is the game's RomFS with files replaced/added from the host (like LayeredFS in Atmosphere or the RomFS mods of other emulators), this is what makes dubs and translations that replace game files work
     * @note The guest reads the RomFS as a raw image, so a brand new image (header, hash tables, metadata tables) is generated and file data is served from the original RomFS or from the host files on demand
     */
    class LayeredRomFsBacking : public Backing {
      private:
        struct FileRegion {
            u64 offset; //!< The offset of the file from the start of the file data
            u64 size;
            bool fromHost;
            u64 baseOffset; //!< The offset in the original image (when not from the host)
            std::string hostPath;
        };

        std::shared_ptr<Backing> base;
        std::vector<u8> metadata; //!< The header and the tables that precede the file data
        u64 dataOffset{};
        std::vector<FileRegion> files; //!< Sorted by offset
        std::mutex fdMutex;
        std::unordered_map<std::string, int> openFiles;

        size_t ReadImpl(span<u8> output, size_t offset) override;

        size_t ReadHostFile(const std::string &path, u8 *output, size_t size, size_t offset);

      public:
        /**
         * @param base The game's RomFS, can be null to build an image that only has the host files
         * @param hostFiles A map of the path of a file in the RomFS (without a leading slash, '/' separated) to the path of the file on the host that replaces it
         */
        LayeredRomFsBacking(std::shared_ptr<Backing> base, const std::map<std::string, std::string> &hostFiles);

        ~LayeredRomFsBacking() override;

        /**
         * @brief Applies the RomFS mods in the mods directory of a title (<modsRoot>/<mod name>/romfs/...) to the original RomFS
         * @return The original RomFS if there's nothing to replace, otherwise a layered one
         */
        static std::shared_ptr<Backing> Create(const std::shared_ptr<Backing> &base, const std::string &titleModsRoot);
    };
}
