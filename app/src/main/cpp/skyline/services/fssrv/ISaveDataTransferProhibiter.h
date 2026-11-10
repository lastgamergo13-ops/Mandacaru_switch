// SPDX-License-Identifier: MPL-2.0
// Copyright © 2026 Mandacaru Contributors

#pragma once

#include <services/serviceman.h>

namespace skyline::service::fssrv {
    /**
     * @brief Stops the transfer of a save data to another console while it's alive, which has no meaning for the emulator so it has no commands (like in Eden)
     * @url https://switchbrew.org/wiki/Filesystem_services#ISaveDataTransferProhibiter
     */
    class ISaveDataTransferProhibiter : public BaseService {
      public:
        ISaveDataTransferProhibiter(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}
    };
}
