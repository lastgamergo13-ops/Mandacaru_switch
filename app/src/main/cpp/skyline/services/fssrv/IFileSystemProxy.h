// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <services/account/IAccountServiceForApplication.h>
#include "IFileSystem.h"

namespace skyline::service::fssrv {
    enum class SaveDataSpaceId : u64 {
        System = 0, //!< Savedata should be stored in the EMMC system folder
        User = 1, //!< Savedata should be stored in the EMMC user folder
        SdSystem = 2, //!< Savedata should be stored in the SDCard system folder
        Temporary = 3, //!< Savedata should be stored in a temporary folder
        SdCache = 4, //!< Savedata should be stored in the SDCard system folder
        ProperSystem = 100, //!< Savedata should be stored in the system partition
    };

    enum class SaveDataType : u8 {
        System = 0, //!< System savedata
        Account = 1, //!< User game savedata
        Bcat = 2, //!< User BCAT savedata
        Device = 3, //!< Device-wide savedata
        Temporary = 4, //!< Temporary savedata
        Cache = 5, //!< Cache savedata
        SystemBcat = 6, //!< Device-wide BCAT savedata
    };

    enum class SaveDataRank : u8 {
        Primary,
        Secondary,
    };

    struct SaveDataAttribute {
        u64 programId; //!< The program ID to store the savedata contents under
        account::UserId userId; //!< The user ID of whom the applications savedata contents should be stored under
        u64 saveDataId; //!< The ID of the savedata
        SaveDataType type; //!< The type of savedata
        SaveDataRank rank; //!< The rank of the savedata
        u16 index; //!< The index of the savedata
        u8 _pad_[0x1A];
    };
    static_assert(sizeof(SaveDataAttribute) == 0x40);

    enum class StorageId : u8 {
        None = 0,
        Host = 1,
        GameCard = 2,
        NandSystem = 3,
        NandUser = 4,
        SdCard = 5,
    };

    /**
     * @url https://switchbrew.org/wiki/Filesystem_services#SaveDataCreationInfo
     */
    struct SaveDataCreationInfo {
        i64 size; //!< The size of the savedata
        i64 journalSize; //!< The size of the journal of the savedata
        i64 blockSize;
        u64 ownerId;
        u32 flags;
        u8 spaceId; //!< The SaveDataSpaceId the savedata is created in
        bool pseudo;
        u8 _pad_[0x1A];
    };
    static_assert(sizeof(SaveDataCreationInfo) == 0x40);

    /**
     * @url https://switchbrew.org/wiki/Filesystem_services#SaveDataCreationInfo2
     */
    struct SaveDataCreationInfo2 {
        u32 version;
        u32 _pad0_;
        SaveDataAttribute attribute;
        i64 size;
        i64 journalSize;
        i64 blockSize;
        u64 ownerId;
        u32 flags;
        u8 spaceId; //!< The SaveDataSpaceId the savedata is created in
        u8 formatType;
        bool pseudo;
        u8 _pad1_;
        u8 _pad2_[0x30];
    };
    static_assert(sizeof(SaveDataCreationInfo2) == 0xA0);

    /**
     * @brief IFileSystemProxy or fsp-srv is responsible for providing handles to file systems
     * @url https://switchbrew.org/wiki/Filesystem_services#fsp-srv
     */
    class IFileSystemProxy : public BaseService {
      public:
        u64 process{}; //!< The PID as set by SetCurrentProcess
        u32 accessLogMode{}; //!< The filesystem log access mode as set by SetGlobalAccessLogMode

        IFileSystemProxy(const DeviceState &state, ServiceManager &manager);

        /**
         * @brief Sets the PID of the process using FS currently
         * @url https://switchbrew.org/wiki/Filesystem_services#SetCurrentProcess
         */
        Result SetCurrentProcess(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns a handle to an instance of #IFileSystem
         * @url https://switchbrew.org/wiki/Filesystem_services#IFileSystem with type SDCard
         */
        Result OpenSdCardFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns a handle to an instance of #IFileSystem for the RomFS of the current process (the same contents #OpenDataStorageByCurrentProcess serves as a storage)
         * @url https://switchbrew.org/wiki/Filesystem_services#OpenDataFileSystemByCurrentProcess
         */
        Result OpenDataFileSystemByCurrentProcess(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Opens the file system of a program with its patch, which is only used for the manual (HTML document) of a program, those aren't available so this always fails like an absent content would
         * @url https://switchbrew.org/wiki/Filesystem_services#OpenFileSystemWithPatch
         */
        Result OpenFileSystemWithPatch(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Creates the directory of a user savedata (the savedata of the application is created when it's opened too, this only makes it exist earlier)
         * @url https://switchbrew.org/wiki/Filesystem_services#CreateSaveDataFileSystem
         */
        Result CreateSaveDataFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Creates the directory of a system savedata
         * @url https://switchbrew.org/wiki/Filesystem_services#CreateSaveDataFileSystemBySystemSaveDataId
         */
        Result CreateSaveDataFileSystemBySystemSaveDataId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Creates the directory of a savedata in the space it specifies
         * @url https://switchbrew.org/wiki/Filesystem_services#CreateSaveDataFileSystemWithCreationInfo2
         */
        Result CreateSaveDataFileSystemWithCreationInfo2(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Extends a savedata, which succeeds without doing anything because the host directory has no size limit (Eden doesn't extend it either)
         * @url https://switchbrew.org/wiki/Filesystem_services#ExtendSaveDataFileSystem
         */
        Result ExtendSaveDataFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Looks for the savedata that matches a filter, there is no index of savedata so none is ever found (like Eden)
         * @url https://switchbrew.org/wiki/Filesystem_services#FindSaveDataWithFilter
         */
        Result FindSaveDataWithFilter(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Filesystem_services#OpenSaveDataTransferProhibiter
         */
        Result OpenSaveDataTransferProhibiter(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns a storage with the RomFS of the program with the given index, only the index of the current program (0) exists
         * @url https://switchbrew.org/wiki/Filesystem_services#OpenDataStorageWithProgramIndex
         */
        Result OpenDataStorageWithProgramIndex(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        Result GetCacheStorageSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns a handle to an instance of #IFileSystem
         * @url https://switchbrew.org/wiki/Filesystem_services#IFileSystem for the requested save data area
         * @url https://switchbrew.org/wiki/Filesystem_services#OpenSaveDataFileSystem
         */
        Result OpenSaveDataFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns a handle to a read-only instance of #IFileSystem
         * @url https://switchbrew.org/wiki/Filesystem_services#IFileSystem for the requested save data area
         * @url https://switchbrew.org/wiki/Filesystem_services#OpenReadOnlySaveDataFileSystem
         */
        Result OpenReadOnlySaveDataFileSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Filesystem_services#OpenSaveDataInfoReader
         */
        Result OpenSaveDataInfoReader(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Filesystem_services#OpenSaveDataInfoReaderBySaveDataSpaceId
         */
        Result OpenSaveDataInfoReaderBySaveDataSpaceId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        Result OpenSaveDataInfoReaderOnlyCacheStorage(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns a handle to an instance of #IStorage
         * @url https://switchbrew.org/wiki/Filesystem_services#IStorage for the application's data storage
         */
        Result OpenDataStorageByCurrentProcess(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns a handle to an instance of #IStorage
         * @url https://switchbrew.org/wiki/Filesystem_services#OpenDataStorageByDataId
         */
        Result OpenDataStorageByDataId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        Result OpenPatchDataStorageByCurrentProcess(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
          * @brief Reads the extra data of a save data (the sizes, the flags and the owner of it), which isn't stored anywhere so it's returned as all zeros like Eden does. It serves commands 0x39, 0x3A, 0x45 and 0x47, that only differ in how the save data is selected
          * @url https://switchbrew.org/wiki/Filesystem_services#ReadSaveDataFileSystemExtraData
          */
        Result ReadSaveDataFileSystemExtraData(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
          * @brief Returns whether exFAT is supported by the SD card file system, which the emulated one (a host folder) always is
          * @url https://switchbrew.org/wiki/Filesystem_services#IsExFatSupported
          */
        Result IsExFatSupported(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
          * @brief Returns whether the SD card can be accessed, #OpenSdCardFileSystem always opens a host folder so it can
          * @url https://switchbrew.org/wiki/Filesystem_services#IsSdCardAccessible
          */
        Result IsSdCardAccessible(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
          * @brief Sets the filesystem log access mode, which is only kept so GetGlobalAccessLogMode returns it (nothing is logged)
          * @url https://switchbrew.org/wiki/Filesystem_services#SetGlobalAccessLogMode
          */
        Result SetGlobalAccessLogMode(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
          * @brief Returns the filesystem log access mode
          * @url https://switchbrew.org/wiki/Filesystem_services#GetGlobalAccessLogMode
          */
        Result GetGlobalAccessLogMode(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
          * @brief Returns the version of the access log and the index of the program in it
          * @url https://switchbrew.org/wiki/Filesystem_services#GetProgramIndexForAccessLog
          */
        Result GetProgramIndexForAccessLog(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
          * @url https://switchbrew.org/wiki/Filesystem_services#OpenMultiCommitManager
          */
        Result OpenMultiCommitManager(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        SERVICE_DECL(
            SFUNC(0x1, IFileSystemProxy, SetCurrentProcess),
            SFUNC(0x2, IFileSystemProxy, OpenDataFileSystemByCurrentProcess),
            SFUNC(0x7, IFileSystemProxy, OpenFileSystemWithPatch),
            SFUNC(0x12, IFileSystemProxy, OpenSdCardFileSystem),
            SFUNC(0x16, IFileSystemProxy, CreateSaveDataFileSystem),
            SFUNC(0x17, IFileSystemProxy, CreateSaveDataFileSystemBySystemSaveDataId),
            SFUNC(0x1B, IFileSystemProxy, IsExFatSupported),
            SFUNC(0x20, IFileSystemProxy, ExtendSaveDataFileSystem),
            SFUNC(0x22, IFileSystemProxy, GetCacheStorageSize),
            SFUNC(0x25, IFileSystemProxy, CreateSaveDataFileSystemWithCreationInfo2),
            SFUNC(0x33, IFileSystemProxy, OpenSaveDataFileSystem),
            SFUNC(0x34, IFileSystemProxy, OpenSaveDataFileSystem), // By the system savedata ID, it takes the same arguments
            SFUNC(0x35, IFileSystemProxy, OpenReadOnlySaveDataFileSystem),
            SFUNC(0x39, IFileSystemProxy, ReadSaveDataFileSystemExtraData),
            SFUNC(0x3A, IFileSystemProxy, ReadSaveDataFileSystemExtraData),
            SFUNC(0x3C, IFileSystemProxy, OpenSaveDataInfoReader),
            SFUNC(0x3D, IFileSystemProxy, OpenSaveDataInfoReaderBySaveDataSpaceId),
            SFUNC(0x3E, IFileSystemProxy, OpenSaveDataInfoReaderOnlyCacheStorage),
            SFUNC(0x43, IFileSystemProxy, FindSaveDataWithFilter),
            SFUNC(0x45, IFileSystemProxy, ReadSaveDataFileSystemExtraData),
            SFUNC(0x47, IFileSystemProxy, ReadSaveDataFileSystemExtraData),
            SFUNC(0x53, IFileSystemProxy, OpenSaveDataTransferProhibiter),
            SFUNC(0xC8, IFileSystemProxy, OpenDataStorageByCurrentProcess),
            SFUNC(0xCA, IFileSystemProxy, OpenDataStorageByDataId),
            SFUNC(0xCB, IFileSystemProxy, OpenPatchDataStorageByCurrentProcess),
            SFUNC(0xCD, IFileSystemProxy, OpenDataStorageWithProgramIndex),
            SFUNC(0x277, IFileSystemProxy, IsSdCardAccessible),
            SFUNC(0x3EC, IFileSystemProxy, SetGlobalAccessLogMode),
            SFUNC(0x3ED, IFileSystemProxy, GetGlobalAccessLogMode),
            SFUNC(0x3F3, IFileSystemProxy, GetProgramIndexForAccessLog),
            SFUNC(0x4B0, IFileSystemProxy, OpenMultiCommitManager)
        )
    };
}
