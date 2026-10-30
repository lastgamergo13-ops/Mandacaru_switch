// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <fstream>
#include <set>
#include <os.h>
#include <loader/loader.h>
#include <kernel/types/KProcess.h>
#include <crypto/key_store.h>
#include <vfs/os_filesystem.h>
#include <vfs/nca.h>

namespace skyline::vfs {
    /**
     * @brief Helpers for the content the user installs (DLC) and the mods the user adds, all of it lives in the public files directory of the app
     */

    /**
     * @return The title id of the running game, it's read from the NPDM and the NACP is only used when that isn't available
     */
    inline u64 GetCurrentTitleId(const DeviceState &state) {
        if (state.process && state.process->npdm.aci0.programId)
            return state.process->npdm.aci0.programId;

        if (state.loader && state.loader->nacp)
            return std::strtoull(state.loader->nacp->GetSaveDataOwnerId().c_str(), nullptr, 16);

        return 0;
    }

    inline std::string GetCurrentTitleIdString(const DeviceState &state) {
        u64 titleId{GetCurrentTitleId(state)};
        return titleId ? fmt::format("{:016X}", titleId) : std::string{};
    }

    /**
     * @return The directory with the mods for the current game (<mod name>/romfs and <mod name>/exefs), empty if the game isn't known
     */
    inline std::string GetModsDirectory(const DeviceState &state) {
        auto titleId{GetCurrentTitleIdString(state)};
        return titleId.empty() ? std::string{} : state.os->publicAppFilesPath + "/mods/" + titleId;
    }

    /**
     * @return The directory the NCAs of the DLC installed for the current game are in, DLC is installed per game (from the game's own options) and doesn't affect other games
     */
    inline std::string GetInstalledContentsPath(const DeviceState &state) {
        return state.os->publicAppFilesPath + "/games/" + GetCurrentTitleIdString(state) + "/dlc/registered/";
    }

    inline std::string GetInstalledTitleKeysPath(const DeviceState &state) {
        return state.os->publicAppFilesPath + "/games/" + GetCurrentTitleIdString(state) + "/dlc/title_keys.txt";
    }

    /**
     * @brief Adds the title keys from the tickets of installed content to the key store, the file has one 'rights id=title key' line (32 hexadecimal digits each) per ticket
     */
    inline void LoadInstalledTitleKeys(crypto::KeyStore &keyStore, const DeviceState &state) {
        std::ifstream file{GetInstalledTitleKeysPath(state)};
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
     * @brief Scans the installed content for the add-on content (DLC) of the current game
     * @return The indices of the DLC, the DLC with an index N has the title id (application id + 0x1000 + N)
     */
    inline std::vector<u32> ListInstalledDlcIndices(const DeviceState &state) {
        std::set<u32> indices;
        u64 baseId{GetCurrentTitleId(state) + 0x1000};
        if (baseId == 0x1000)
            return {};

        try {
            auto fileSystem{std::make_shared<OsFileSystem>(GetInstalledContentsPath(state))};
            auto directory{fileSystem->OpenDirectory("")};
            auto keyStore{std::make_shared<crypto::KeyStore>(state.os->privateAppFilesPath + "keys")};
            LoadInstalledTitleKeys(*keyStore, state);

            for (const auto &entry : directory->Read()) {
                try {
                    auto nca{NCA(fileSystem->OpenFile(entry.name), keyStore)};
                    if (nca.header.programId > baseId && nca.header.programId < baseId + 0x1000)
                        indices.insert(static_cast<u32>(nca.header.programId - baseId));
                } catch (const std::exception &) {
                    continue; // Not an NCA we can read
                }
            }
        } catch (const std::exception &) {}

        return {indices.begin(), indices.end()};
    }
}
