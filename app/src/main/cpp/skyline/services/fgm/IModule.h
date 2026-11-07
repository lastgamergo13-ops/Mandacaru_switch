// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <services/serviceman.h>

namespace skyline::service::fgm {
    /**
     * @brief IRequest is how a game asks for a share of the frequency of a clock (the Fgm "request"), the emulator doesn't limit anything so every request is accepted
     * @url https://switchbrew.org/wiki/FGM_services
     */
    class IRequest : public BaseService {
      public:
        IRequest(const DeviceState &state, ServiceManager &manager);

        /**
         * @brief Starts a request, the event it returns is signalled all the time as the request is never pending
         */
        Result Initialize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Sets the frequency that is wanted, ignored
         */
        Result Set(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Gets the frequency that was given, 0 as there is no limit
         */
        Result Get(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Cancels the request, nothing to do
         */
        Result Cancel(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        SERVICE_DECL(
            SFUNC(0x0, IRequest, Initialize),
            SFUNC(0x1, IRequest, Set),
            SFUNC(0x2, IRequest, Get),
            SFUNC(0x3, IRequest, Cancel)
        )

      private:
        std::shared_ptr<type::KEvent> event;
    };

    /**
     * @brief IModule is fgm, fgm:0 and fgm:9, the service where a game opens an IRequest
     */
    class IModule : public BaseService {
      public:
        IModule(const DeviceState &state, ServiceManager &manager);

        /**
         * @brief Opens an IRequest
         */
        Result Initialize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        SERVICE_DECL(
            SFUNC(0x0, IModule, Initialize)
        )
    };
}
