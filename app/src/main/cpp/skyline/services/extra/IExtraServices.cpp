// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <bit>
#include <cstring>
#include <kernel/types/KProcess.h>
#include <common/settings.h>
#include <loader/loader.h>
#include "IExtraServices.h"

namespace skyline::service {
    namespace pdm {
        namespace {
            struct PlayStatistics {
                u64 applicationId;
                u32 firstEntryIndex;
                u32 firstTimestampUser;
                u32 firstTimestampNetwork;
                u32 lastEntryIndex;
                u32 lastTimestampUser;
                u32 lastTimestampNetwork;
                u32 playTimeInMinutes;
                u32 totalLaunches;
            };
            static_assert(sizeof(PlayStatistics) == 0x28);

            struct ApplicationPlayStatistics {
                u64 applicationId;
                u64 playTimeNs;
                u64 launchCount;
            };
            static_assert(sizeof(ApplicationPlayStatistics) == 0x18);
        }

        INotifyService::INotifyService(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

        Result INotifyService::Notify(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            return {};
        }

        IQueryService::IQueryService(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

        Result IQueryService::QueryPlayStatisticsByApplicationIdAndUserAccountId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            request.Pop<u8>();
            request.Skip<std::array<u8, 7>>();
            u64 applicationId{request.Pop<u64>()};

            PlayStatistics statistics{};
            statistics.applicationId = applicationId;
            statistics.totalLaunches = 1;
            response.Push(statistics);
            return {};
        }

        Result IQueryService::QueryLastPlayTime(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            i32 entries{};
            if (!request.inputBuf.empty() && !request.outputBuf.empty()) {
                entries = static_cast<i32>(request.inputBuf.at(0).size() / sizeof(i32));
                std::memset(request.outputBuf.at(0).data(), 0, request.outputBuf.at(0).size());
            }
            response.Push<i32>(entries);
            return {};
        }

        Result IQueryService::QueryApplicationPlayStatisticsForSystem(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            i32 entries{};
            if (!request.inputBuf.empty() && !request.outputBuf.empty()) {
                auto ids{request.inputBuf.at(0).cast<u64>()};
                auto output{request.outputBuf.at(0).cast<ApplicationPlayStatistics>()};
                size_t count{std::min(ids.size(), output.size())};
                for (size_t i{}; i < count; i++)
                    output[i] = ApplicationPlayStatistics{.applicationId = ids[i], .playTimeNs = 0, .launchCount = 1};
                entries = static_cast<i32>(count);
            }
            response.Push<i32>(entries);
            return {};
        }
    }

    namespace ovln {
        ISender::ISender(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

        Result ISender::Send(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            return {};
        }

        ISenderService::ISenderService(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

        Result ISenderService::OpenSender(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            manager.RegisterService(SRVREG(ISender), session, response);
            return {};
        }
    }

    namespace pm {
        IInformationInterface::IInformationInterface(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

        Result IInformationInterface::GetProgramId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.Push<u64>(state.process->npdm.aci0.programId);
            return {};
        }
    }

    namespace nfc {
        IUser::IUser(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager), availabilityChangeEvent{std::make_shared<type::KEvent>(state, false)} {}

        Result IUser::Initialize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            nfcState = 1;
            return {};
        }

        Result IUser::Finalize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            nfcState = 0;
            return {};
        }

        Result IUser::GetState(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.Push<u32>(nfcState);
            return {};
        }

        Result IUser::IsNfcEnabled(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.Push<u8>(1);
            return {};
        }

        Result IUser::ListDevices(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.Push<i32>(0);
            return {};
        }

        Result IUser::AttachAvailabilityChangeEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.copyHandles.push_back(state.process->InsertItem(availabilityChangeEvent));
            return {};
        }
    }

    namespace ns {
        namespace {
            constexpr size_t NacpSize{0x4000};
            constexpr i64 StorageTotalSize{64LL * 1024 * 1024 * 1024};
            constexpr i64 StorageFreeSize{32LL * 1024 * 1024 * 1024};

            /**
             * @brief Writes the NACP of the running game to the first output buffer
             * @return false if the buffer is too small
             */
            bool WriteControlData(const DeviceState &state, ipc::IpcRequest &request, ipc::IpcResponse &response) {
                request.Pop<u8>(); // The source of the data (local, remote...), the game that runs is the only one that exists
                request.Skip<std::array<u8, 7>>();
                request.Pop<u64>(); // The application id

                if (request.outputBuf.empty() || request.outputBuf.at(0).size() < NacpSize)
                    return false;

                auto output{request.outputBuf.at(0)};
                if (state.loader && state.loader->nacp)
                    std::memcpy(output.data(), &state.loader->nacp->nacpContents, NacpSize);
                else
                    std::memset(output.data(), 0, NacpSize);

                response.Push<u32>(static_cast<u32>(NacpSize)); // There is no icon after the NACP
                return true;
            }

            u8 DesiredLanguage(const DeviceState &state, u32 supportedLanguages) {
                auto desired{language::GetApplicationLanguage(*state.settings->systemLanguage)};
                if (supportedLanguages != 0 && ((1U << static_cast<u32>(desired)) & supportedLanguages) == 0)
                    desired = static_cast<language::ApplicationLanguage>(std::countr_zero(supportedLanguages));
                return static_cast<u8>(desired);
            }
        }

        IApplicationManagerInterface::IApplicationManagerInterface(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

        Result IApplicationManagerInterface::GetApplicationControlData(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            if (!WriteControlData(state, request, response))
                return Result{1, 1}; // The buffer isn't big enough for the NACP
            return {};
        }

        Result IApplicationManagerInterface::GetApplicationDesiredLanguage(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.Push<u8>(DesiredLanguage(state, request.Pop<u32>()));
            return {};
        }

        Result IApplicationManagerInterface::ConvertApplicationLanguageToLanguageCode(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            auto applicationLanguage{static_cast<language::ApplicationLanguage>(request.Pop<u8>())};
            response.Push<u64>(language::GetLanguageCode(language::GetSystemLanguage(applicationLanguage)));
            return {};
        }

        Result IApplicationManagerInterface::GetTotalSpaceSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.Push<i64>(StorageTotalSize);
            return {};
        }

        Result IApplicationManagerInterface::GetFreeSpaceSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.Push<i64>(StorageFreeSize);
            return {};
        }

        Result IApplicationManagerInterface::CheckSdCardMountStatus(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            return {};
        }

        Result IApplicationManagerInterface::IsAnyApplicationEntityInstalled(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.Push<u8>(1);
            return {};
        }

        Result IApplicationManagerInterface::CheckApplicationLaunchVersion(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            return {};
        }

        IReadOnlyApplicationControlDataInterface::IReadOnlyApplicationControlDataInterface(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

        Result IReadOnlyApplicationControlDataInterface::GetApplicationControlData(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            if (!WriteControlData(state, request, response))
                return Result{1, 1};
            return {};
        }

        Result IReadOnlyApplicationControlDataInterface::GetApplicationDesiredLanguage(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            response.Push<u8>(DesiredLanguage(state, request.Pop<u32>()));
            return {};
        }

        Result IReadOnlyApplicationControlDataInterface::ConvertApplicationLanguageToLanguageCode(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            auto applicationLanguage{static_cast<language::ApplicationLanguage>(request.Pop<u8>())};
            response.Push<u64>(language::GetLanguageCode(language::GetSystemLanguage(applicationLanguage)));
            return {};
        }

        IServiceGetterInterface::IServiceGetterInterface(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

        Result IServiceGetterInterface::GetApplicationManagerInterface(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            manager.RegisterService(SRVREG(IApplicationManagerInterface), session, response);
            return {};
        }

        Result IServiceGetterInterface::GetReadOnlyApplicationControlDataInterface(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            manager.RegisterService(SRVREG(IReadOnlyApplicationControlDataInterface), session, response);
            return {};
        }
    }

    namespace placeholder {
        IPlaceholderService::IPlaceholderService(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

        Result IPlaceholderService::Nothing(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
            return {};
        }
    }
}
