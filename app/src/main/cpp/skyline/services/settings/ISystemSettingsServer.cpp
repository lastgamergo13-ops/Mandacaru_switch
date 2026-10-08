// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <os.h>
#include <crypto/key_store.h>
#include <vfs/os_filesystem.h>
#include <vfs/nca.h>
#include <vfs/rom_filesystem.h>
#include "ISystemSettingsServer.h"

namespace skyline::service::settings {
    ISystemSettingsServer::ISystemSettingsServer(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

    /**
     * @brief Reads the System Version title (0x0100000000000809) from the firmware installed by the user
     * @return If the version was successfully read, this fails (without throwing) if no firmware is installed or its keys are missing
     */
    static bool ReadInstalledSystemVersion(const DeviceState &state, ISystemSettingsServer::SysVerTitle &systemVersion) {
        constexpr u64 SystemVersionProgramId{0x0100000000000809};

        try {
            auto systemArchivesFileSystem{std::make_shared<vfs::OsFileSystem>(state.os->publicAppFilesPath + "/switch/nand/system/Contents/registered/")};
            auto systemArchives{systemArchivesFileSystem->OpenDirectory("")};
            auto keyStore{std::make_shared<crypto::KeyStore>(state.os->privateAppFilesPath + "keys")};

            for (const auto &entry : systemArchives->Read()) {
                try {
                    std::shared_ptr<vfs::Backing> backing{systemArchivesFileSystem->OpenFile(entry.name)};
                    auto nca{vfs::NCA(backing, keyStore)};

                    if (nca.header.programId == SystemVersionProgramId && nca.romFs != nullptr) {
                        auto romFs{std::make_shared<vfs::RomFileSystem>(nca.romFs)};
                        romFs->OpenFile("file")->Read<ISystemSettingsServer::SysVerTitle>(systemVersion);
                        return true;
                    }
                } catch (const std::exception &) {
                    continue; // NCAs that can't be opened (e.g. missing keys) can't be the one we're looking for
                }
            }
        } catch (const std::exception &) {}

        return false;
    }

    Result ISystemSettingsServer::GetFirmwareVersion(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        SysVerTitle systemVersion{};

        // Report the version of the firmware the user installed, otherwise fall back to a fixed recent version
        if (!ReadInstalledSystemVersion(state, systemVersion))
            systemVersion = {.major=22, .minor=5, .micro=0, .revMajor=1, .revMinor=0, .platform="NX", .verHash="0000000000000000000000000000000000000000", .dispVer="22.5.0", .dispTitle="NintendoSDK Firmware for NX 22.5.0-1.0"};

        request.outputBuf.at(0).as<SysVerTitle>() = systemVersion;
        return {};
    }

    Result ISystemSettingsServer::GetColorSetId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u32>(0); // Basic White
        return {};
    }
}
