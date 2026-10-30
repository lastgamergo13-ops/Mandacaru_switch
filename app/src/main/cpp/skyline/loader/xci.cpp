// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <common.h>
#include <kernel/types/KProcess.h>
#include <vfs/region_backing.h>
#include <vfs/ncz_backing.h>
#include "nca.h"
#include "xci.h"

namespace skyline::loader {
    XciLoader::XciLoader(const std::shared_ptr<vfs::Backing> &backing, const std::shared_ptr<crypto::KeyStore> &keyStore) {
        header = backing->Read<GamecardHeader>();

        if (header.magic != util::MakeMagic<u32>("HEAD"))
            throw exception("Invalid XCI file");

        xci = std::make_shared<vfs::PartitionFileSystem>(std::make_shared<vfs::RegionBacking>(backing, header.hfs0PartitionOffset, header.hfs0HeaderSize * sizeof(u32)));

        auto root{xci->OpenDirectory("", {false, true})};
        for (const auto &entry : root->Read()) {
            auto entryDir{std::make_shared<vfs::PartitionFileSystem>(xci->OpenFile(entry.name))};
            if (entry.name == "secure")
                secure = entryDir;
            else if (entry.name == "normal")
                normal = entryDir;
            else if (entry.name == "update")
                update = entryDir;
            else if (entry.name == "logo")
                logo = entryDir;
        }

        if (secure) {
            root = secure->OpenDirectory("", {false, true});
            for (const auto &entry : root->Read()) {
                // XCZ files hold NCZ (block compressed NCA) files next to the regular ones
                auto extension{entry.name.substr(entry.name.find_last_of('.') + 1)};
                if (extension != "nca" && extension != "ncz")
                    continue;

                try {
                    std::shared_ptr<vfs::Backing> ncaBacking{secure->OpenFile(entry.name)};
                    if (extension == "ncz")
                        ncaBacking = std::make_shared<vfs::NczBacking>(std::move(ncaBacking));

                    auto nca{vfs::NCA(std::move(ncaBacking), keyStore, true)};

                    if (nca.contentType == vfs::NcaContentType::Program && nca.romFs != nullptr && nca.exeFs != nullptr)
                        programNca = std::move(nca);
                    else if (nca.contentType == vfs::NcaContentType::Control && nca.romFs != nullptr)
                        controlNca = std::move(nca);
                } catch (const loader_exception &e) {
                    throw loader_exception(e.error);
                } catch (const std::exception &e) {
                    continue;
                }
            }
        } else {
            throw exception("Corrupted secure partition");
        }

        if (!programNca || !controlNca)
            throw exception("Incomplete XCI file");

        romFs = programNca->romFs;
        controlRomFs = std::make_shared<vfs::RomFileSystem>(controlNca->romFs);
        nacp.emplace(controlRomFs->OpenFile("control.nacp"));
    }

    void *XciLoader::LoadProcessData(const std::shared_ptr<kernel::type::KProcess> &process, const DeviceState &state) {
        process->npdm = vfs::NPDM(programNca->exeFs->OpenFile("main.npdm"));
        return NcaLoader::LoadExeFs(this, programNca->exeFs, process, state);
    }

    std::vector<u8> XciLoader::GetIcon(language::ApplicationLanguage language) {
        if (controlRomFs == nullptr)
            return std::vector<u8>();

        std::shared_ptr<vfs::Backing> icon{};

        auto iconName{fmt::format("icon_{}.dat", language::ToString(language))};
        if (!(icon = controlRomFs->OpenFileUnchecked(iconName, {true, false, false}))) {
            iconName = fmt::format("icon_{}.dat", language::ToString(nacp->GetFirstSupportedTitleLanguage()));
            icon = controlRomFs->OpenFileUnchecked(iconName, {true, false, false});
        }

        if (icon == nullptr)
            return std::vector<u8>();

        std::vector<u8> buffer(icon->size);
        icon->Read(buffer);
        return buffer;
    }
}