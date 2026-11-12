// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <services/base_service.h>

namespace skyline::service::spl {
    /**
     * @brief The secure monitor interface (spl:, spl:mig, spl:fs, spl:ssl, spl:es, spl:manu), games only use it to get random bytes and to ask what kind of console they run on
     * @note Behaviour follows what Eden does for "spl:", the key related commands (GenerateAesKek, LoadAesKey...) aren't implemented there either and are answered with a success and no output (BaseService::HandleRequest)
     * @url https://switchbrew.org/wiki/SPL_services
     */
    class ISecureProcessor : public BaseService {
      public:
        ISecureProcessor(const DeviceState &state, ServiceManager &manager);

        /**
         * @brief Returns the value of a secure monitor configuration item: a retail unit (no development hardware, no program verification disabled) with the values of a regular Switch
         */
        Result GetConfig(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Fills an output buffer with random bytes
         */
        Result GenerateRandomBytes(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns if this is a development unit, which it never is
         */
        Result IsDevelopment(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        SERVICE_DECL(
            SFUNC(0x0, ISecureProcessor, GetConfig),
            SFUNC(0x7, ISecureProcessor, GenerateRandomBytes),
            SFUNC(0xB, ISecureProcessor, IsDevelopment)
        )
    };
}
