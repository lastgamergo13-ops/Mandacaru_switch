// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <services/serviceman.h>

/**
 * @file Small services that games open at startup (or while playing) but that have little to do in the emulator: they exist so that the game gets a session instead of an error. Behaviours follow what Eden does for the same services.
 * @note A command that isn't listed in a service is answered with a success and no output (BaseService::HandleRequest), the same way Eden answers the commands it has no code for
 */
namespace skyline::service {
    namespace pdm {
        /**
         * @brief pdm:ntfy is where the system is told about play events (applet changes, mode changes), nothing is recorded
         * @url https://switchbrew.org/wiki/PDM_services
         */
        class INotifyService : public BaseService {
          public:
            INotifyService(const DeviceState &state, ServiceManager &manager);

            /**
             * @brief Any of the notify commands, they are accepted and ignored
             */
            Result Notify(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x0, INotifyService, Notify),
                SFUNC(0x2, INotifyService, Notify),
                SFUNC(0x3, INotifyService, Notify),
                SFUNC(0x4, INotifyService, Notify),
                SFUNC(0x5, INotifyService, Notify),
                SFUNC(0x6, INotifyService, Notify),
                SFUNC(0x7, INotifyService, Notify),
                SFUNC(0x8, INotifyService, Notify)
            )
        };

        /**
         * @brief pdm:qry answers the questions about the play statistics: the game was launched once and has no recorded play time
         */
        class IQueryService : public BaseService {
          public:
            IQueryService(const DeviceState &state, ServiceManager &manager);

            /**
             * @brief Play statistics of an application for a user (0x28 bytes): only the application id and one launch are filled
             */
            Result QueryPlayStatisticsByApplicationIdAndUserAccountId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief Last play time of the applications in the input buffer, the output has no entries filled in
             */
            Result QueryLastPlayTime(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief Play statistics (0x18 bytes each) of the applications of the input buffer: no play time and one launch
             */
            Result QueryApplicationPlayStatisticsForSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x5, IQueryService, QueryPlayStatisticsByApplicationIdAndUserAccountId),
                SFUNC(0x11, IQueryService, QueryLastPlayTime),
                SFUNC(0x12, IQueryService, QueryApplicationPlayStatisticsForSystem)
            )
        };
    }

    namespace ovln {
        /**
         * @brief ISender is where a game sends overlay notifications (the ones the system shows over the game): they are dropped
         */
        class ISender : public BaseService {
          public:
            ISender(const DeviceState &state, ServiceManager &manager);

            /**
             * @brief Accepts a message
             */
            Result Send(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x0, ISender, Send)
            )
        };

        /**
         * @brief ovln:snd opens the ISender of a game
         */
        class ISenderService : public BaseService {
          public:
            ISenderService(const DeviceState &state, ServiceManager &manager);

            /**
             * @brief Opens an ISender
             */
            Result OpenSender(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x0, ISenderService, OpenSender)
            )
        };
    }

    namespace pm {
        /**
         * @brief pm:info gives information about the processes, there is only the game
         * @url https://switchbrew.org/wiki/Process_Manager_services
         */
        class IInformationInterface : public BaseService {
          public:
            IInformationInterface(const DeviceState &state, ServiceManager &manager);

            /**
             * @brief Returns the program id (title id) of a process, the id of the game for any process id
             */
            Result GetProgramId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x0, IInformationInterface, GetProgramId)
            )
        };
    }

    namespace nfc {
        /**
         * @brief nfc:user is the NFC reader for applications that read tags that aren't amiibo. There is no reader: it is available, but no device is ever listed
         * @url https://switchbrew.org/wiki/NFC_services
         */
        class IUser : public BaseService {
          public:
            IUser(const DeviceState &state, ServiceManager &manager);

            Result Initialize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            Result Finalize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief 0 when not initialized and 1 after Initialize
             */
            Result GetState(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            Result IsNfcEnabled(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief No device is ever listed
             */
            Result ListDevices(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief The event that is signalled when the availability of the reader changes, it never is
             */
            Result AttachAvailabilityChangeEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x0, IUser, Initialize),
                SFUNC(0x1, IUser, Finalize),
                SFUNC(0x2, IUser, GetState),
                SFUNC(0x3, IUser, IsNfcEnabled),
                SFUNC(0x190, IUser, Initialize),
                SFUNC(0x191, IUser, Finalize),
                SFUNC(0x192, IUser, GetState),
                SFUNC(0x193, IUser, IsNfcEnabled),
                SFUNC(0x194, IUser, ListDevices),
                SFUNC(0x197, IUser, AttachAvailabilityChangeEvent)
            )

          private:
            u32 nfcState{}; //!< 0 is not initialized, 1 is initialized
            std::shared_ptr<type::KEvent> availabilityChangeEvent;
        };
    }

    namespace ns {
        /**
         * @brief IApplicationManagerInterface is the part of the ns services that games use: the NACP of the game, the desired language and the space of the storages
         * @url https://switchbrew.org/wiki/NS_Services#IApplicationManagerInterface
         */
        class IApplicationManagerInterface : public BaseService {
          public:
            IApplicationManagerInterface(const DeviceState &state, ServiceManager &manager);

            /**
             * @brief Writes the NACP of the game (0x4000 bytes, there is no icon) to the output buffer
             * @note The NACP is the one of the game that runs, whatever application id is asked for
             */
            Result GetApplicationControlData(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief The language the game should use: the one of the system if the game supports it, else the first one it supports
             */
            Result GetApplicationDesiredLanguage(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            Result ConvertApplicationLanguageToLanguageCode(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief Total size of a storage (the SD card or the internal one): 64 GiB
             */
            Result GetTotalSpaceSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief Free space of a storage: 32 GiB
             */
            Result GetFreeSpaceSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief Success: there is a SD card
             */
            Result CheckSdCardMountStatus(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            Result IsAnyApplicationEntityInstalled(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief Always accepts the launch version
             */
            Result CheckApplicationLaunchVersion(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x2B, IApplicationManagerInterface, CheckSdCardMountStatus),
                SFUNC(0x2F, IApplicationManagerInterface, GetTotalSpaceSize),
                SFUNC(0x30, IApplicationManagerInterface, GetFreeSpaceSize),
                SFUNC(0x26, IApplicationManagerInterface, CheckApplicationLaunchVersion),
                SFUNC(0x37, IApplicationManagerInterface, GetApplicationDesiredLanguage),
                SFUNC(0x3B, IApplicationManagerInterface, ConvertApplicationLanguageToLanguageCode),
                SFUNC(0x190, IApplicationManagerInterface, GetApplicationControlData),
                SFUNC(0x514, IApplicationManagerInterface, IsAnyApplicationEntityInstalled)
            )
        };

        /**
         * @brief IReadOnlyApplicationControlDataInterface has the same control data functions in the form that applications use
         */
        class IReadOnlyApplicationControlDataInterface : public BaseService {
          public:
            IReadOnlyApplicationControlDataInterface(const DeviceState &state, ServiceManager &manager);

            Result GetApplicationControlData(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            Result GetApplicationDesiredLanguage(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            Result ConvertApplicationLanguageToLanguageCode(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x0, IReadOnlyApplicationControlDataInterface, GetApplicationControlData),
                SFUNC(0x1, IReadOnlyApplicationControlDataInterface, GetApplicationDesiredLanguage),
                SFUNC(0x2, IReadOnlyApplicationControlDataInterface, ConvertApplicationLanguageToLanguageCode)
            )
        };

        /**
         * @brief ns:am2, ns:ec, ns:rid, ns:rt, ns:web and ns:ro: opens the interfaces of the ns service
         */
        class IServiceGetterInterface : public BaseService {
          public:
            IServiceGetterInterface(const DeviceState &state, ServiceManager &manager);

            Result GetApplicationManagerInterface(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            Result GetReadOnlyApplicationControlDataInterface(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x1F35, IServiceGetterInterface, GetReadOnlyApplicationControlDataInterface),
                SFUNC(0x1F3C, IServiceGetterInterface, GetApplicationManagerInterface)
            )
        };
    }

    namespace placeholder {
        /**
         * @brief A service that is only there to be opened, used for the ones that Eden has no code for either (erpt:c, erpt:r, mnpp:app, bsdcfg, pcv, es): every command is answered with a success and no output
         */
        class IPlaceholderService : public BaseService {
          public:
            IPlaceholderService(const DeviceState &state, ServiceManager &manager);

            /**
             * @brief Does nothing, it is the only function so the table isn't empty
             */
            Result Nothing(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0xFFFFFF, IPlaceholderService, Nothing)
            )
        };
    }
}
