// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <filesystem>
#include <fstream>
#include <vfs/nca.h>
#include <vfs/os_filesystem.h>
#include <crypto/key_store.h>

namespace skyline::loader {
    /**
     * @brief The NCAs of the update of a game that the user installed (from the options of the game, they're copied with their tickets to games/<title id>/dlc)
     */
    struct InstalledUpdate {
        std::optional<vfs::NCA> program; //!< The program NCA of the update, it has the new ExeFS and the RomFS made from the base game's one
        std::optional<vfs::NCA> control; //!< The control NCA of the update, it has the new version and name of the game
    };

    /**
     * @brief Adds the title keys of the tickets that were installed together with the content ('rights id=title key' lines)
     */
    inline void LoadInstalledTitleKeys(crypto::KeyStore &keyStore, const std::string &titleKeysPath) {
        std::ifstream file{titleKeysPath};
        std::string line;
        while (std::getline(file, line)) {
            size_t separator{line.find('=')};
            if (separator == std::string::npos)
                continue;

            try {
                std::string_view rightsId{std::string_view(line).substr(0, separator)};
                std::string_view titleKey{std::string_view(line).substr(separator + 1)};
                while (!titleKey.empty() && (titleKey.back() == '\r' || titleKey.back() == ' '))
                    titleKey.remove_suffix(1);
                keyStore.PopulateTitleKey(util::HexStringToArray<16>(rightsId), util::HexStringToArray<16>(titleKey));
            } catch (const std::exception &) {
                continue; // A malformed line only affects that ticket
            }
        }
    }

    /**
     * @brief Looks for the update of a game in the content the user installed, the title id of an update is the one of the game + 0x800
     * @param publicFilesPath The public files directory of the app, with a slash at the end
     * @note If several updates are installed the one that was installed last is used
     */
    inline InstalledUpdate FindInstalledUpdate(const std::string &publicFilesPath, u64 baseTitleId, const std::shared_ptr<crypto::KeyStore> &keyStore) {
        InstalledUpdate update;
        if (!keyStore || baseTitleId == 0)
            return update;

        std::string root{publicFilesPath};
        if (!root.empty() && root.back() != '/')
            root += '/';
        std::string gameDirectory{fmt::format("{}games/{:016X}/dlc/", root, baseTitleId)};
        std::string contentsDirectory{gameDirectory + "registered/"};
        if (!std::filesystem::is_directory(contentsDirectory))
            return update;

        LoadInstalledTitleKeys(*keyStore, gameDirectory + "title_keys.txt");

        u64 updateTitleId{baseTitleId + 0x800};
        std::filesystem::file_time_type programTime{}, controlTime{};
        auto fileSystem{std::make_shared<vfs::OsFileSystem>(contentsDirectory)};
        for (const auto &entry : fileSystem->OpenDirectory("")->Read()) {
            try {
                auto nca{vfs::NCA(fileSystem->OpenFile(entry.name), keyStore)};
                if (nca.header.programId != updateTitleId)
                    continue;

                auto time{std::filesystem::last_write_time(contentsDirectory + entry.name)};
                if (nca.contentType == vfs::NcaContentType::Program && nca.exeFs != nullptr && (nca.HasPatchedRomFs() || nca.romFs != nullptr) && (!update.program || time > programTime)) {
                    update.program = std::move(nca);
                    programTime = time;
                } else if (nca.contentType == vfs::NcaContentType::Control && nca.romFs != nullptr && (!update.control || time > controlTime)) {
                    update.control = std::move(nca);
                    controlTime = time;
                }
            } catch (const std::exception &) {
                continue; // DLC and anything that can't be read (missing keys) isn't an update we can use
            }
        }

        return update;
    }
}
