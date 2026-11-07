// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <services/serviceman.h>

namespace skyline::service::am {
    /**
     * @brief This has functions used to retrieve the status of the application's window
     * @url https://switchbrew.org/wiki/Applet_Manager_services#IWindowController
     */
    class IWindowController : public BaseService {
      public:
        IWindowController(const DeviceState &state, ServiceManager &manager);

        /**
         * @brief Returns the PID of the current application
         * @url https://switchbrew.org/wiki/Applet_Manager_services#GetAppletResourceUserId
         */
        Result GetAppletResourceUserId(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief This function has no inputs or outputs (Stubbed)
         * @url https://switchbrew.org/wiki/Applet_Manager_services#AcquireForegroundRights
         */
        Result AcquireForegroundRights(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns the ARUID of the applet that launched the game, 0 as the game is launched by the emulator
         * @url https://switchbrew.org/wiki/Applet_Manager_services#GetAppletResourceUserIdOfCallerApplet
         */
        Result GetAppletResourceUserIdOfCallerApplet(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        SERVICE_DECL(
            SFUNC(0x1, IWindowController, GetAppletResourceUserId),
            SFUNC(0x2, IWindowController, GetAppletResourceUserIdOfCallerApplet),
            SFUNC(0xA, IWindowController, AcquireForegroundRights)
        )
    };
}
