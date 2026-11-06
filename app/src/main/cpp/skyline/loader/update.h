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
     * @brief What the content meta (CNMT) of an update says about its files
     * @note The NCAs of an update carry the title id of the GAME in their header, only the meta NCA has the title id of the update (game + 0x800), so the files can't be told apart by looking at their headers: the meta lists the ones that belong to the update
     */
    struct UpdateMeta {
        u32 version{}; //!< The version of the update
        std::array<u8, 0x10> programContentId{}; //!< The content id (the name of the NCA file without the extension) of the program NCA
        std::array<u8, 0x10> controlContentId{}; //!< The content id of the control NCA
        bool hasProgram{}, hasControl{};
        std::filesystem::file_time_type time{}; //!< When the meta file was installed, used if two updates have the same version
    };

    inline std::string ContentIdToFileName(const std::array<u8, 0x10> &contentId) {
        std::string name;
        for (u8 byte : contentId)
            name += fmt::format("{:02x}", byte);
        return name + ".nca";
    }

    /**
     * @brief Reads the content records of the CNMT file inside a meta NCA
     * @return The files of the update, empty if this isn't the meta of the update with the given title id
     */
    inline std::optional<UpdateMeta> ReadUpdateMeta(vfs::NCA &metaNca, u64 updateTitleId) {
        if (metaNca.cnmt == nullptr)
            return std::nullopt;

        for (const auto &entry : metaNca.cnmt->OpenDirectory("", {false, true})->Read()) {
            if (entry.name.size() < 5 || entry.name.substr(entry.name.size() - 5) != ".cnmt")
                continue;

            auto file{metaNca.cnmt->OpenFile(entry.name)};
            constexpr size_t HeaderSize{0x20}, RecordSize{0x38};
            if (file->size < HeaderSize || file->size > 0x100000)
                continue;

            std::vector<u8> data(file->size);
            file->Read(data);

            u64 titleId;
            std::memcpy(&titleId, data.data(), sizeof(titleId));
            if (titleId != updateTitleId)
                continue;

            UpdateMeta meta;
            std::memcpy(&meta.version, data.data() + 0x8, sizeof(meta.version));
            u16 extendedHeaderSize, contentCount;
            std::memcpy(&extendedHeaderSize, data.data() + 0xE, sizeof(extendedHeaderSize));
            std::memcpy(&contentCount, data.data() + 0x10, sizeof(contentCount));

            size_t position{HeaderSize + extendedHeaderSize};
            for (u16 i{}; i < contentCount && position + RecordSize <= data.size(); i++, position += RecordSize) {
                // A record is the hash (0x20 bytes), the content id (0x10), the size (6) and the type (1) and an id offset (1)
                u8 type{data[position + 0x36]};
                std::array<u8, 0x10> contentId;
                std::memcpy(contentId.data(), data.data() + position + 0x20, contentId.size());

                if (type == 1) { // Program
                    meta.programContentId = contentId;
                    meta.hasProgram = true;
                } else if (type == 3) { // Control
                    meta.controlContentId = contentId;
                    meta.hasControl = true;
                }
            }

            if (meta.hasProgram)
                return meta;
        }
        return std::nullopt;
    }

    /**
     * @brief Looks for the update of a game in the content the user installed, the title id of an update is the one of the game + 0x800
     * @param publicFilesPath The public files directory of the app, with a slash at the end
     * @note The files of the update are the ones its content meta lists. If no meta can be read the newest program NCA with a patched RomFS (what only an update has) and the newest control NCA are used
     * @note If several updates are installed the one with the highest version is used (the one that was installed last if they have the same version)
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
        if (!std::filesystem::is_directory(contentsDirectory)) {
            LOGI("No installed content for {:016X} (looked in {})", baseTitleId, contentsDirectory);
            return update;
        }

        LoadInstalledTitleKeys(*keyStore, gameDirectory + "title_keys.txt");
        LOGI("Looking for the update of {:016X} (title id {:016X}) in {}", baseTitleId, baseTitleId + 0x800, contentsDirectory);

        u64 updateTitleId{baseTitleId + 0x800};
        auto fileSystem{std::make_shared<vfs::OsFileSystem>(contentsDirectory)};
        auto entries{fileSystem->OpenDirectory("")->Read()};

        // First the meta of the update, it says which files are the program and the control NCA
        std::optional<UpdateMeta> bestMeta;
        for (const auto &entry : entries) {
            try {
                auto nca{vfs::NCA(fileSystem->OpenFile(entry.name), keyStore)};
                LOGI("  {}: title id {:016X}, content type {}, ExeFS: {}, RomFS: {}", entry.name, nca.header.programId, static_cast<int>(nca.contentType), nca.exeFs != nullptr, nca.romFs != nullptr || nca.HasPatchedRomFs());

                if (nca.contentType != vfs::NcaContentType::Meta || nca.header.programId != updateTitleId)
                    continue;

                auto meta{ReadUpdateMeta(nca, updateTitleId)};
                if (!meta)
                    continue;

                meta->time = std::filesystem::last_write_time(contentsDirectory + entry.name);
                LOGI("  {}: the content meta of the update version {} lists the program NCA {} and the control NCA {}", entry.name, meta->version, ContentIdToFileName(meta->programContentId), meta->hasControl ? ContentIdToFileName(meta->controlContentId) : "(none)");
                if (!bestMeta || meta->version > bestMeta->version || (meta->version == bestMeta->version && meta->time > bestMeta->time))
                    bestMeta = meta;
            } catch (const std::exception &e) {
                // DLC and anything that can't be read (missing keys, a table that couldn't be understood) isn't an update we can use, the reason is in the log
                LOGW("  {} can't be used: {}", entry.name, e.what());
            }
        }

        auto openByContentId{[&](const std::array<u8, 0x10> &contentId) -> std::optional<vfs::NCA> {
            std::string name{ContentIdToFileName(contentId)};
            if (!fileSystem->FileExists(name))
                return std::nullopt;
            try {
                return vfs::NCA(fileSystem->OpenFile(name), keyStore);
            } catch (const std::exception &e) {
                LOGW("  {} can't be used: {}", name, e.what());
                return std::nullopt;
            }
        }};

        if (bestMeta) {
            auto program{openByContentId(bestMeta->programContentId)};
            if (program && program->contentType == vfs::NcaContentType::Program && program->exeFs != nullptr && (program->HasPatchedRomFs() || program->romFs != nullptr))
                update.program = std::move(program);
            else
                LOGW("  The program NCA that the update lists ({}) is missing or can't be used", ContentIdToFileName(bestMeta->programContentId));

            if (bestMeta->hasControl) {
                auto control{openByContentId(bestMeta->controlContentId)};
                if (control && control->contentType == vfs::NcaContentType::Control && control->romFs != nullptr)
                    update.control = std::move(control);
            }
        }

        if (!update.program) {
            // No usable meta: the update is the newest program NCA that has a patched RomFS (the title id in the header is the one of the game, or the one of the update in some dumps)
            std::filesystem::file_time_type programTime{}, controlTime{};
            for (const auto &entry : entries) {
                try {
                    auto nca{vfs::NCA(fileSystem->OpenFile(entry.name), keyStore)};
                    if (nca.header.programId != updateTitleId && nca.header.programId != baseTitleId)
                        continue;

                    auto time{std::filesystem::last_write_time(contentsDirectory + entry.name)};
                    if (nca.contentType == vfs::NcaContentType::Program && nca.exeFs != nullptr && nca.HasPatchedRomFs() && (!update.program || time > programTime)) {
                        update.program = std::move(nca);
                        programTime = time;
                    } else if (nca.contentType == vfs::NcaContentType::Control && nca.romFs != nullptr && (!update.control || time > controlTime)) {
                        update.control = std::move(nca);
                        controlTime = time;
                    }
                } catch (const std::exception &) {
                    continue;
                }
            }
        }

        LOGI("Update of {:016X}: program NCA {}, control NCA {}", baseTitleId, update.program ? "found" : "NOT found", update.control ? "found" : "NOT found");
        return update;
    }
}
