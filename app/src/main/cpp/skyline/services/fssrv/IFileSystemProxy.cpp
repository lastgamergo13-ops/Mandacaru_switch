// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <cstring>
#include <os.h>
#include <vfs/os_filesystem.h>
#include <vfs/rom_filesystem.h>
#include <vfs/nca.h>
#include <vfs/layered_romfs.h>
#include <vfs/installed_content.h>
#include <loader/loader.h>
#include "results.h"
#include "IStorage.h"
#include "IMultiCommitManager.h"
#include "ISaveDataTransferProhibiter.h"
#include "IFileSystemProxy.h"
#include "ISaveDataInfoReader.h"

namespace skyline::service::fssrv {
    IFileSystemProxy::IFileSystemProxy(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

    Result IFileSystemProxy::SetCurrentProcess(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        process = request.Pop<u64>();
        return {};
    }

    Result IFileSystemProxy::OpenSdCardFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        manager.RegisterService(std::make_shared<IFileSystem>(std::make_shared<vfs::OsFileSystem>(state.os->publicAppFilesPath + "/switch/sdmc/"), state, manager), session, response);
        return {};
    }

    Result IFileSystemProxy::GetCacheStorageSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u64>(0);
        response.Push<u64>(0);
        return {};
    }

    /**
     * @return The path of the directory of the savedata, relative to the directory of the emulated Switch
     */
    static std::string GetSaveDataPath(const DeviceState &state, SaveDataSpaceId spaceId, SaveDataAttribute attribute) {
        if (attribute.programId == 0)
            attribute.programId = state.loader->nacp->nacpContents.saveDataOwnerId;

        std::string spaceIdStr{[spaceId]() {
            switch (spaceId) {
                case SaveDataSpaceId::System:
                    return "/nand/system";
                case SaveDataSpaceId::User:
                    return "/nand/user";
                case SaveDataSpaceId::Temporary:
                    return "/nand/temp";
                default:
                    throw exception("Unsupported savedata ID: {}", spaceId);
            }
        }()};

        switch (attribute.type) {
            case SaveDataType::System:
                return fmt::format("{}/save/{:016X}/{:016X}{:016X}/", spaceIdStr, attribute.saveDataId, attribute.userId.lower, attribute.userId.upper);
            case SaveDataType::Account:
            case SaveDataType::Device:
                return fmt::format("{}/save/{:016X}/{:016X}{:016X}/{:016X}/", spaceIdStr, 0, attribute.userId.lower, attribute.userId.upper, attribute.programId);
            case SaveDataType::Temporary:
                return fmt::format("{}/{:016X}/{:016X}{:016X}/{:016X}/", spaceIdStr, 0, attribute.userId.lower, attribute.userId.upper, attribute.programId);
            case SaveDataType::Cache:
                return fmt::format("{}/save/cache/{:016X}/", spaceIdStr, attribute.programId);
            default:
                throw exception("Unsupported savedata type: {}", attribute.type);
        }
    }

    Result IFileSystemProxy::OpenSaveDataFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto spaceId{request.Pop<SaveDataSpaceId>()};
        auto attribute{request.Pop<SaveDataAttribute>()};

        auto saveDataPath{GetSaveDataPath(state, spaceId, attribute)};

        manager.RegisterService(std::make_shared<IFileSystem>(std::make_shared<vfs::OsFileSystem>(state.os->publicAppFilesPath + "/switch" + saveDataPath), state, manager), session, response);
        return {};
    }

    /**
     * @brief Creates the directory of a savedata, a savedata that can't be placed on the host (like the ones of SD card spaces) is reported as created because nothing can use it
     */
    static void CreateSaveDataDirectory(const DeviceState &state, SaveDataSpaceId spaceId, const SaveDataAttribute &attribute) {
        try {
            vfs::OsFileSystem{state.os->publicAppFilesPath + "/switch" + GetSaveDataPath(state, spaceId, attribute)}; // The directory is created when it's opened
        } catch (const std::exception &e) {
            LOGW("Cannot create the savedata: {}", e.what());
        }
    }

    Result IFileSystemProxy::CreateSaveDataFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        request.Skip<SaveDataCreationInfo>();
        auto attribute{request.Pop<SaveDataAttribute>()};
        // The user ID the savedata is for follows, which is the same one that is in the attribute for the savedata of a user

        CreateSaveDataDirectory(state, SaveDataSpaceId::User, attribute);
        return {};
    }

    Result IFileSystemProxy::CreateSaveDataFileSystemBySystemSaveDataId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto attribute{request.Pop<SaveDataAttribute>()};

        CreateSaveDataDirectory(state, SaveDataSpaceId::System, attribute);
        return {};
    }

    Result IFileSystemProxy::CreateSaveDataFileSystemWithCreationInfo2(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto creationInfo{request.Pop<SaveDataCreationInfo2>()};

        CreateSaveDataDirectory(state, static_cast<SaveDataSpaceId>(creationInfo.spaceId), creationInfo.attribute);
        return {};
    }

    Result IFileSystemProxy::ExtendSaveDataFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return {};
    }

    Result IFileSystemProxy::FindSaveDataWithFilter(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return result::EntityNotFound;
    }

    Result IFileSystemProxy::OpenSaveDataTransferProhibiter(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        manager.RegisterService(SRVREG(ISaveDataTransferProhibiter), session, response);
        return {};
    }

    Result IFileSystemProxy::OpenReadOnlySaveDataFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        // Forward to OpenSaveDataFileSystem for now.
        // TODO: This should wrap the underlying filesystem with nn::fs::ReadOnlyFileSystem.
        return OpenSaveDataFileSystem(session, request, response);
    }

    Result IFileSystemProxy::OpenSaveDataInfoReader(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        manager.RegisterService(SRVREG(ISaveDataInfoReader), session, response);
        return {};
    }

    Result IFileSystemProxy::OpenSaveDataInfoReaderBySaveDataSpaceId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        manager.RegisterService(SRVREG(ISaveDataInfoReader), session, response);
        return {};
    }

    Result IFileSystemProxy::OpenSaveDataInfoReaderOnlyCacheStorage(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        manager.RegisterService(SRVREG(ISaveDataInfoReader), session, response);
        return {};
    }

    /**
     * @return The RomFS of the game with the RomFS mods (dubs, translations...) from the mods directory applied, it's only built once
     */
    static std::shared_ptr<vfs::Backing> GetModdedRomFs(const DeviceState &state) {
        static std::mutex mutex;
        static const loader::Loader *cachedLoader{};
        static std::shared_ptr<vfs::Backing> cachedRomFs;

        std::scoped_lock lock{mutex};
        if (cachedLoader != state.loader.get() || !cachedRomFs) {
            cachedLoader = state.loader.get();
            cachedRomFs = state.loader->romFs;

            auto modsDirectory{vfs::GetModsDirectory(state)};
            if (!modsDirectory.empty()) {
                try {
                    cachedRomFs = vfs::LayeredRomFsBacking::Create(state.loader->romFs, modsDirectory);
                } catch (const std::exception &e) {
                    LOGW("Failed to apply the RomFS mods, using the original RomFS: {}", e.what());
                }
            }
        }
        return cachedRomFs;
    }

    Result IFileSystemProxy::OpenDataStorageByCurrentProcess(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        if (!state.loader->romFs)
            return result::NoRomFsAvailable;

        manager.RegisterService(std::make_shared<IStorage>(GetModdedRomFs(state), state, manager), session, response);
        return {};
    }

    Result IFileSystemProxy::OpenDataFileSystemByCurrentProcess(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        if (!state.loader->romFs)
            return result::NoRomFsAvailable;

        manager.RegisterService(std::make_shared<IFileSystem>(std::make_shared<vfs::RomFileSystem>(GetModdedRomFs(state)), state, manager), session, response);
        return {};
    }

    Result IFileSystemProxy::OpenFileSystemWithPatch(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto type{request.Pop<u32>()};
        auto programId{request.Pop<u64>()};
        LOGW("OpenFileSystemWithPatch is only available for the manual of a program which isn't supported, type: {}, program ID: 0x{:X}", type, programId);
        return result::NoRomFsAvailable;
    }

    Result IFileSystemProxy::OpenDataStorageWithProgramIndex(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto programIndex{request.Pop<u8>()};
        if (programIndex != 0) {
            LOGW("Cannot open the storage of the program with index {}, only the current program (0) is available", programIndex);
            return result::NoRomFsAvailable;
        }

        return OpenDataStorageByCurrentProcess(session, request, response);
    }

    Result IFileSystemProxy::OpenDataStorageByDataId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto storageId{request.Pop<StorageId>()};
        request.Skip<std::array<u8, 7>>(); // 7-bytes padding
        auto dataId{request.Pop<u64>()};

        // Add-on content (DLC) installed by the user is mounted by its title id
        try {
            auto contentFileSystem{std::make_shared<vfs::OsFileSystem>(vfs::GetInstalledContentsPath(state))};
            auto contents{contentFileSystem->OpenDirectory("")};
            auto contentKeyStore{std::make_shared<skyline::crypto::KeyStore>(state.os->privateAppFilesPath + "keys")};
            vfs::LoadInstalledTitleKeys(*contentKeyStore, state);

            for (const auto &entry : contents->Read()) {
                try {
                    auto nca{vfs::NCA(contentFileSystem->OpenFile(entry.name), contentKeyStore)};
                    if (nca.header.programId == dataId && nca.romFs != nullptr) {
                        manager.RegisterService(std::make_shared<IStorage>(nca.romFs, state, manager), session, response);
                        return {};
                    }
                } catch (const std::exception &) {
                    continue;
                }
            }
        } catch (const std::exception &) {}

        auto systemArchivesFileSystem{std::make_shared<vfs::OsFileSystem>(state.os->publicAppFilesPath + "/switch/nand/system/Contents/registered/")};
        auto systemArchives{systemArchivesFileSystem->OpenDirectory("")};
        auto keyStore{std::make_shared<skyline::crypto::KeyStore>(state.os->privateAppFilesPath + "keys")};

        for (const auto &entry : systemArchives->Read()) {
            try {
                std::shared_ptr<vfs::Backing> backing{systemArchivesFileSystem->OpenFile(entry.name)};
                auto nca{vfs::NCA(backing, keyStore)};

                if (nca.header.programId == dataId && nca.romFs != nullptr) {
                    manager.RegisterService(std::make_shared<IStorage>(nca.romFs, state, manager), session, response);
                    return {};
                }
            } catch (const std::exception &) {
                continue; // An NCA we can't open (e.g. missing keys for a newer key generation) must not make the whole lookup fail
            }
        }

        auto romFs{std::make_shared<IStorage>(state.os->assetFileSystem->OpenFile(fmt::format("romfs/{:016X}", dataId)), state, manager)};

        manager.RegisterService(romFs, session, response);
        return {};
    }

    Result IFileSystemProxy::OpenPatchDataStorageByCurrentProcess(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return result::EntityNotFound;
    }

    Result IFileSystemProxy::ReadSaveDataFileSystemExtraData(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        // The caller's buffer would keep whatever was in it otherwise
        for (auto &buffer : request.outputBuf)
            std::memset(buffer.data(), 0, buffer.size());
        return {};
    }

    Result IFileSystemProxy::IsExFatSupported(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u8>(true);
        return {};
    }

    Result IFileSystemProxy::IsSdCardAccessible(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u8>(true);
        return {};
    }

    Result IFileSystemProxy::SetGlobalAccessLogMode(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        accessLogMode = request.Pop<u32>();
        return {};
    }

    Result IFileSystemProxy::GetGlobalAccessLogMode(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u32>(accessLogMode);
        return {};
    }

    Result IFileSystemProxy::GetProgramIndexForAccessLog(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        constexpr u32 AccessLogVersionLatest{2}; // 7.0.0, the same as Eden

        response.Push<u32>(AccessLogVersionLatest);
        response.Push<u32>(0); // The program index, a program that isn't one of several in the same application has 0
        return {};
    }

    Result IFileSystemProxy::OpenMultiCommitManager(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        manager.RegisterService(SRVREG(IMultiCommitManager), session, response);
        return {};
    }
}
