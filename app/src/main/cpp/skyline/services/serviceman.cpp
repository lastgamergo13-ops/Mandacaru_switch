// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <array>
#include <mutex>
#include <string>
#include <kernel/types/KProcess.h>
#include <kernel/results.h>
#include <common/trace.h>
#include "sm/IUserInterface.h"
#include "settings/ISettingsServer.h"
#include "settings/ISystemSettingsServer.h"
#include "apm/IManager.h"
#include "am/IApplicationProxyService.h"
#include "am/IAllSystemAppletProxiesService.h"
#include "audio/IAudioInManager.h"
#include "audio/IAudioOutManager.h"
#include "audio/IAudioRendererManager.h"
#include "bcat/IServiceCreator.h"
#include "codec/IHardwareOpusDecoderManager.h"
#include "fatalsrv/IService.h"
#include "hid/IHidServer.h"
#include "irs/IIrSensorServer.h"
#include "irs/iirsensor_core.h"
#include "timesrv/IStaticService.h"
#include "glue/IStaticService.h"
#include "glue/IWriterForSystem.h"
#include "glue/INotificationServicesForApplication.h"
#include "services/timesrv/core.h"
#include "fssrv/IFileSystemProxy.h"
#include "nvdrv/INvDrvServices.h"
#include "nvdrv/driver.h"
#include "hosbinder/IHOSBinderDriver.h"
#include "visrv/IApplicationRootService.h"
#include "visrv/ISystemRootService.h"
#include "visrv/IManagerRootService.h"
#include "pl/IPlatformServiceManager.h"
#include "pl/shared_font_core.h"
#include "aocsrv/IAddOnContentManager.h"
#include "pctl/IParentalControlServiceFactory.h"
#include "lbl/ILblController.h"
#include "lm/ILogService.h"
#include "ldn/IUserServiceCreator.h"
#include "account/IAccountServiceForApplication.h"
#include "friends/IServiceCreator.h"
#include "nfp/IUserManager.h"
#include "nifm/IStaticService.h"
#include "nim/IShopServiceAccessServerInterface.h"
#include "socket/bsd/IClient.h"
#include "socket/nsd/IManager.h"
#include "socket/sfdnsres/IResolver.h"
#include "spl/IRandomInterface.h"
#include "spl/ISecureProcessor.h"
#include "ssl/ISslService.h"
#include "prepo/IPrepoService.h"
#include "mmnv/IRequest.h"
#include "bt/IBluetoothUser.h"
#include "btm/IBtmUser.h"
#include "capsrv/IAlbumAccessorService.h"
#include "capsrv/ICaptureControllerService.h"
#include "capsrv/IAlbumApplicationService.h"
#include "capsrv/IScreenShotApplicationService.h"
#include "ro/IRoInterface.h"
#include "mii/IStaticService.h"
#include "olsc/IOlscServiceForApplication.h"
#include "clkrst/IClkrstManager.h"
#include "ts/IMeasurementServer.h"
#include "ngc/INgcService.h"
#include "fgm/IModule.h"
#include "extra/IExtraServices.h"
#include "psm/IPsmServer.h"
#include "ntc/IEnsureNetworkClockAvailabilityService.h"
#include "serviceman.h"

#define SERVICE_CASE(class, name, ...) \
    case util::MakeMagic<ServiceName>(name): { \
            std::shared_ptr<BaseService> serviceObject{std::make_shared<class>(state, *this, ##__VA_ARGS__)}; \
            serviceMap[util::MakeMagic<ServiceName>(name)] = serviceObject; \
            return serviceObject; \
        }

namespace skyline::service {
    namespace {
        /**
         * @brief Keeps the last IPC requests so the log can show what happened right before an error (a request on a closed session, for example). The first requests are also written to the log as they happen (Info), later session events only at Debug level
         */
        class IpcTrace {
          private:
            std::mutex traceMutex;
            std::array<std::string, 256> recent;
            size_t next{};
            u32 written{};

          public:
            void Add(u32 thread, std::string line, bool always = false) {
                line = fmt::format("[thread {}] {}", thread, line); // Needed to tell what a frozen thread asked from the noise of the others (the audio threads make hundreds of requests per second)
                std::scoped_lock lock{traceMutex};
                if (written < 400) {
                    written++;
                    LOGI("IPC: {}", line);
                } else if (always) {
                    // Games can poll services in a tight loop (Pokémon Brilliant Diamond opens/closes audio sessions ~50 times a second), logging every session event at Info flooded the log with 180k+ lines and slowed the game down
                    LOGD("IPC: {}", line);
                }
                recent[next++ % recent.size()] = std::move(line);
            }

            void DumpRecent() {
                std::scoped_lock lock{traceMutex};
                LOGW("IPC: the last {} requests before this error (identical consecutive requests are collapsed):", std::min(next, recent.size()));
                size_t first{next > recent.size() ? next - recent.size() : 0};
                for (size_t i{first}; i < next;) {
                    const auto &line{recent[i % recent.size()]};
                    size_t repeats{1};
                    while (i + repeats < next && recent[(i + repeats) % recent.size()] == line)
                        repeats++;
                    if (repeats > 1)
                        LOGW("IPC:   {} (x{})", line, repeats);
                    else
                        LOGW("IPC:   {}", line);
                    i += repeats;
                }
            }
        } ipcTrace;
    }

    void DumpRecentIpcTrace() {
        ipcTrace.DumpRecent();
    }

    struct GlobalServiceState {
        timesrv::core::TimeServiceObject timesrv;
        pl::SharedFontCore sharedFontCore;
        irs::SharedIirCore sharedIirCore;
        nvdrv::Driver nvdrv;

        explicit GlobalServiceState(const DeviceState &state) : timesrv(state), sharedFontCore(state), sharedIirCore(state), nvdrv(state) {}
    };

    ServiceManager::ServiceManager(const DeviceState &state) : state(state), smUserInterface(std::make_shared<sm::IUserInterface>(state, *this)), globalServiceState(std::make_shared<GlobalServiceState>(state)) {}

    std::shared_ptr<BaseService> ServiceManager::CreateOrGetService(ServiceName name) {
        auto serviceIter{serviceMap.find(name)};
        if (serviceIter != serviceMap.end())
            return (*serviceIter).second;

        switch (name) {
            SERVICE_CASE(fatalsrv::IService, "fatal:u")
            SERVICE_CASE(settings::ISettingsServer, "set")
            SERVICE_CASE(settings::ISystemSettingsServer, "set:sys")
            SERVICE_CASE(apm::IManager, "apm")
            SERVICE_CASE(am::IApplicationProxyService, "appletOE")
            SERVICE_CASE(am::IAllSystemAppletProxiesService, "appletAE")
            SERVICE_CASE(audio::IAudioInManager, "audin:u")
            SERVICE_CASE(audio::IAudioOutManager, "audout:u")
            SERVICE_CASE(audio::IAudioRendererManager, "audren:u")
            SERVICE_CASE(codec::IHardwareOpusDecoderManager, "hwopus")
            SERVICE_CASE(hid::IHidServer, "hid")
            SERVICE_CASE(irs::IIrSensorServer, "irs", globalServiceState->sharedIirCore)
            SERVICE_CASE(timesrv::IStaticService, "time:s", globalServiceState->timesrv, timesrv::constant::StaticServiceSystemPermissions) // Both of these would be registered after TimeServiceManager::Setup normally but we call that in the GlobalServiceState constructor so can just list them here directly
            SERVICE_CASE(timesrv::IStaticService, "time:su", globalServiceState->timesrv, timesrv::constant::StaticServiceSystemUpdatePermissions)
            SERVICE_CASE(glue::IStaticService, "time:a", globalServiceState->timesrv.managerServer.GetStaticServiceAsAdmin(state, *this), globalServiceState->timesrv, timesrv::constant::StaticServiceAdminPermissions)
            SERVICE_CASE(glue::IStaticService, "time:r", globalServiceState->timesrv.managerServer.GetStaticServiceAsRepair(state, *this), globalServiceState->timesrv, timesrv::constant::StaticServiceRepairPermissions)
            SERVICE_CASE(glue::IStaticService, "time:u", globalServiceState->timesrv.managerServer.GetStaticServiceAsUser(state, *this), globalServiceState->timesrv, timesrv::constant::StaticServiceUserPermissions)
            SERVICE_CASE(glue::INotificationServicesForApplication, "notif:a")
            SERVICE_CASE(glue::IWriterForSystem, "ectx:w")
            SERVICE_CASE(glue::IWriterForSystem, "ectx:aw")
            SERVICE_CASE(fssrv::IFileSystemProxy, "fsp-srv")
            SERVICE_CASE(nvdrv::INvDrvServices, "nvdrv", globalServiceState->nvdrv, nvdrv::ApplicationSessionPermissions)
            SERVICE_CASE(nvdrv::INvDrvServices, "nvdrv:a", globalServiceState->nvdrv, nvdrv::AppletSessionPermissions)
            SERVICE_CASE(hosbinder::IHOSBinderDriver, "dispdrv", globalServiceState->nvdrv.core.nvMap)
            SERVICE_CASE(visrv::IApplicationRootService, "vi:u")
            SERVICE_CASE(visrv::ISystemRootService, "vi:s")
            SERVICE_CASE(visrv::IManagerRootService, "vi:m")
            SERVICE_CASE(pl::IPlatformServiceManager, "pl:u", globalServiceState->sharedFontCore)
            SERVICE_CASE(aocsrv::IAddOnContentManager, "aoc:u")
            SERVICE_CASE(pctl::IParentalControlServiceFactory, "pctl")
            SERVICE_CASE(pctl::IParentalControlServiceFactory, "pctl:a")
            SERVICE_CASE(pctl::IParentalControlServiceFactory, "pctl:s")
            SERVICE_CASE(pctl::IParentalControlServiceFactory, "pctl:r")
            SERVICE_CASE(lbl::ILblController, "lbl")
            SERVICE_CASE(lm::ILogService, "lm")
            SERVICE_CASE(ldn::IUserServiceCreator, "ldn:u")
            SERVICE_CASE(account::IAccountServiceForApplication, "acc:u0")
            SERVICE_CASE(account::IAccountServiceForApplication, "acc:u1")
            SERVICE_CASE(friends::IServiceCreator, "friend:u")
            SERVICE_CASE(nfp::IUserManager, "nfp:user")
            SERVICE_CASE(nifm::IStaticService, "nifm:u")
            SERVICE_CASE(nifm::IStaticService, "nifm:s")
            SERVICE_CASE(nifm::IStaticService, "nifm:a")
            SERVICE_CASE(ngc::INgcService, "ngc:u")
            SERVICE_CASE(ngc::ITextFilter, "ngct:u")
            SERVICE_CASE(fgm::IModule, "fgm")
            SERVICE_CASE(fgm::IModule, "fgm:0")
            SERVICE_CASE(fgm::IModule, "fgm:9")
            SERVICE_CASE(pdm::INotifyService, "pdm:ntfy")
            SERVICE_CASE(pdm::IQueryService, "pdm:qry")
            SERVICE_CASE(ovln::ISenderService, "ovln:snd")
            SERVICE_CASE(pm::IInformationInterface, "pm:info")
            SERVICE_CASE(nfc::IUser, "nfc:user")
            SERVICE_CASE(ns::IServiceGetterInterface, "ns:am2")
            SERVICE_CASE(ns::IServiceGetterInterface, "ns:ec")
            SERVICE_CASE(ns::IServiceGetterInterface, "ns:rid")
            SERVICE_CASE(ns::IServiceGetterInterface, "ns:rt")
            SERVICE_CASE(ns::IServiceGetterInterface, "ns:web")
            SERVICE_CASE(ns::IServiceGetterInterface, "ns:ro")
            SERVICE_CASE(placeholder::IPlaceholderService, "erpt:c")
            SERVICE_CASE(placeholder::IPlaceholderService, "erpt:r")
            SERVICE_CASE(placeholder::IPlaceholderService, "mnpp:app")
            SERVICE_CASE(placeholder::IPlaceholderService, "bsdcfg")
            SERVICE_CASE(placeholder::IPlaceholderService, "pcv")
            SERVICE_CASE(placeholder::IPlaceholderService, "es")
            SERVICE_CASE(socket::IClient, "bsd:u")
            SERVICE_CASE(socket::IClient, "bsd:s")
            SERVICE_CASE(socket::IManager, "nsd:u")
            SERVICE_CASE(socket::IManager, "nsd:a")
            SERVICE_CASE(socket::IResolver, "sfdnsres")
            SERVICE_CASE(spl::IRandomInterface, "csrng")
            SERVICE_CASE(spl::ISecureProcessor, "spl:")
            SERVICE_CASE(spl::ISecureProcessor, "spl:mig")
            SERVICE_CASE(spl::ISecureProcessor, "spl:fs")
            SERVICE_CASE(spl::ISecureProcessor, "spl:ssl")
            SERVICE_CASE(spl::ISecureProcessor, "spl:es")
            SERVICE_CASE(spl::ISecureProcessor, "spl:manu")
            SERVICE_CASE(ssl::ISslService, "ssl")
            SERVICE_CASE(ssl::ISslService, "ssl:s")
            SERVICE_CASE(prepo::IPrepoService, "prepo:u")
            SERVICE_CASE(prepo::IPrepoService, "prepo:a")
            SERVICE_CASE(mmnv::IRequest, "mm:u")
            SERVICE_CASE(bcat::IServiceCreator, "bcat:u")
            SERVICE_CASE(bt::IBluetoothUser, "bt")
            SERVICE_CASE(btm::IBtmUser, "btm:u")
            SERVICE_CASE(capsrv::IAlbumAccessorService, "caps:a")
            SERVICE_CASE(capsrv::ICaptureControllerService, "caps:c")
            SERVICE_CASE(capsrv::IAlbumApplicationService, "caps:u")
            SERVICE_CASE(capsrv::IScreenShotApplicationService, "caps:su")
            SERVICE_CASE(nim::IShopServiceAccessServerInterface, "nim:eca")
            SERVICE_CASE(ro::IRoInterface, "ldr:ro")
            SERVICE_CASE(mii::IStaticService, "mii:e")
            SERVICE_CASE(mii::IStaticService, "mii:u")
            SERVICE_CASE(olsc::IOlscServiceForApplication, "olsc:u")
            SERVICE_CASE(clkrst::IClkrstManager, "clkrst")
            SERVICE_CASE(ts::IMeasurementServer, "ts")
            SERVICE_CASE(psm::IPsmServer, "psm")
            SERVICE_CASE(ntc::IEnsureNetworkClockAvailabilityService, "ntc")
            default:
                std::string_view nameString(span(reinterpret_cast<char *>(&name), sizeof(name)).as_string(true));
                throw std::out_of_range(fmt::format("CreateService called with an unknown service name: {}", nameString));
        }
    }

    std::shared_ptr<BaseService> ServiceManager::NewService(ServiceName name, type::KSession &session, ipc::IpcResponse &response) {
        std::scoped_lock serviceGuard{mutex};
        auto serviceObject{CreateOrGetService(name)};
        KHandle handle{};
        if (session.isDomain) {
            handle = session.domainTable->Add(serviceObject);
            response.domainObjects.push_back(handle);
        } else {
            handle = state.process->NewHandle<type::KSession>(serviceObject).handle;
            response.moveHandles.push_back(handle);
        }
        LOGD("Service has been created: \"{}\" (0x{:X})", serviceObject->GetName(), handle);
        ipcTrace.Add(state.thread ? state.thread->id : 0, fmt::format("service \"{}\" created, {} 0x{:X}", serviceObject->GetName(), session.isDomain ? "domain object" : "handle", handle), true);
        return serviceObject;
    }

    void ServiceManager::RegisterService(std::shared_ptr<BaseService> serviceObject, type::KSession &session, ipc::IpcResponse &response) { // NOLINT(performance-unnecessary-value-param)
        std::scoped_lock serviceGuard{mutex};
        KHandle handle{};

        if (session.isDomain) {
            handle = session.domainTable->Add(serviceObject);
            response.domainObjects.push_back(handle);
        } else {
            handle = state.process->NewHandle<type::KSession>(serviceObject).handle;
            response.moveHandles.push_back(handle);
        }

        LOGD("Service has been registered: \"{}\" (0x{:X})", serviceObject->GetName(), handle);
    }

    void ServiceManager::CloseSession(KHandle handle) {
        std::scoped_lock serviceGuard{mutex};
        auto session{state.process->GetHandle<type::KSession>(handle)};
        if (session->isOpen) {
            if (session->isDomain) {
                for (const auto &domainService : session->domainTable->Snapshot())
                    std::erase_if(serviceMap, [domainService](const auto &entry) {
                        return entry.second == domainService;
                    });
            } else {
                std::erase_if(serviceMap, [session](const auto &entry) {
                    return entry.second == session->serviceObject;
                });
            }
            session->isOpen = false;
        }
    }

    Result ServiceManager::SyncRequestHandler(KHandle handle) {
        TRACE_EVENT("kernel", "ServiceManager::SyncRequestHandler");
        auto session{state.process->GetHandle<type::KSession>(handle)};
        LOGV("----IPC Start----");
        LOGV("Handle is 0x{:X}", handle);

        if (session->isOpen) {
            ipc::IpcRequest request(session->isDomain, state);
            ipc::IpcResponse response(state);

            ipcTrace.Add(state.thread ? state.thread->id : 0, fmt::format("handle 0x{:X} ({}{}) type {} {}{}", handle, session->serviceObject ? session->serviceObject->GetName() : "?", session->isDomain ? ", domain" : "", static_cast<u16>(request.header->type), request.payload ? "command " : "", request.payload ? static_cast<i64>(request.payload->value) : -1), request.header->type != ipc::CommandType::Request && request.header->type != ipc::CommandType::RequestWithContext);

            switch (request.header->type) {
                case ipc::CommandType::Request:
                case ipc::CommandType::RequestWithContext:
                    if (session->isDomain) {
                        try {
                            auto service{session->domainTable->Get(request.domain->objectId)};
                            if (service == nullptr)
                                throw exception("Domain request used an expired handle");
                            switch (request.domain->command) {
                                case ipc::DomainCommand::SendMessage:
                                    response.errorCode = service->HandleRequest(*session, request, response);
                                    break;

                                case ipc::DomainCommand::CloseVHandle:
                                    std::erase_if(serviceMap, [service](const auto &entry) {
                                        return entry.second == service;
                                    });
                                    session->domainTable->Reset(request.domain->objectId);
                                    break;
                            }
                        } catch (std::out_of_range &) {
                            throw exception("Invalid object ID was used with domain request");
                        }
                    } else {
                        response.errorCode = session->serviceObject->HandleRequest(*session, request, response);
                    }
                    response.WriteResponse(session->isDomain);
                    break;

                case ipc::CommandType::Control:
                case ipc::CommandType::ControlWithContext:
                    LOGD("Control IPC Message: 0x{:X}", request.payload->value);
                    switch (static_cast<ipc::ControlCommand>(request.payload->value)) {
                        case ipc::ControlCommand::ConvertCurrentObjectToDomain:
                            response.Push(session->ConvertDomain());
                            break;

                        case ipc::ControlCommand::CloneCurrentObject:
                        case ipc::ControlCommand::CloneCurrentObjectEx: {
                            // The clone is a session of its own that talks to the same service object. It used to be the very same KSession under a second handle, so when the game closed the original (the Nintendo SDK does this right after cloning) the clone was closed too and every later request on it was dropped ("svcSendSyncRequest called on closed handle")
                            std::shared_ptr<BaseService> cloneServiceObject{session->serviceObject};
                            auto clone{state.process->NewHandle<type::KSession>(cloneServiceObject)};
                            // On real hardware a clone of a domain session is still a domain session (it shares the domain table of the original, so objects created through either session work on both), so its requests carry the domain header. Without this the header was parsed as the command payload and every request became command 0, which no service has ("Cannot find HIPC function ... 0x0") and made the SDK abort right after the FS init
                            clone.item->isDomain = session->isDomain;
                            clone.item->domainTable = session->domainTable;
                            KHandle cloneHandle{clone.handle};
                            response.moveHandles.push_back(cloneHandle);
                            ipcTrace.Add(state.thread ? state.thread->id : 0, fmt::format("handle 0x{:X} cloned into handle 0x{:X}", handle, cloneHandle), true);
                            break;
                        }

                        case ipc::ControlCommand::QueryPointerBufferSize:
                            response.Push<u32>(0x8000);
                            break;

                        default:
                            throw exception("Unknown Control Command: {}", request.payload->value);
                    }
                    response.WriteResponse(false);
                    break;

                case ipc::CommandType::Close:
                case ipc::CommandType::TipcCloseSession:
                    LOGD("Closing Session");
                    ipcTrace.Add(state.thread ? state.thread->id : 0, fmt::format("handle 0x{:X} closed by the game", handle), true);
                    CloseSession(handle);
                    break;
                default:
                    // TIPC command ID is encoded in the request type
                    if (request.isTipc) {
                        response.errorCode = session->serviceObject->HandleRequest(*session, request, response);
                        response.WriteResponse(session->isDomain, true);
                    } else {
                        throw exception("Unimplemented IPC message type: {}", static_cast<u16>(request.header->type));
                    }
            }
        } else {
            LOGW("svcSendSyncRequest called on closed handle: 0x{:X}", handle);
            ipcTrace.DumpRecent();
            // A closed session answers with an error, returning success made the game read the reply of an older request as if it was the answer
            return kernel::result::SessionClosed;
        }
        LOGV("====IPC End====");
        return {};
    }
}
