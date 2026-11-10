// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)
// Copyright © 2026 Mandacaru Contributors

#pragma once

#include <services/serviceman.h>

namespace skyline {
    namespace constant {
        constexpr std::string_view DefaultAudioInName{"DeviceIn"}; //!< The default audio input device name
        constexpr std::string_view UacAudioInName{"UacIn"}; //!< The name of an audio input device that is connected over USB audio class
    }

    namespace service::audio {
        /**
         * @brief IAudioInManager or audin:u is used to manage audio inputs (the microphone)
         * @url https://switchbrew.org/wiki/Audio_services#audin:u
         */
        class IAudioInManager : public BaseService {
          public:
            IAudioInManager(const DeviceState &state, ServiceManager &manager);

            /**
             * @brief Returns a list of all available audio inputs (it also serves the Auto versions)
             * @url https://switchbrew.org/wiki/Audio_services#ListAudioIns
             */
            Result ListAudioIns(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief Creates a new #IAudioIn object and returns a handle to it (it also serves the Auto version)
             * @url https://switchbrew.org/wiki/Audio_services#OpenAudioIn
             */
            Result OpenAudioIn(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            /**
             * @brief Does the same as #OpenAudioIn but takes the protocol of the audio input too, which is only used to tell a USB one apart
             * @url https://switchbrew.org/wiki/Audio_services#OpenAudioInProtocolSpecified
             */
            Result OpenAudioInProtocolSpecified(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

            SERVICE_DECL(
                SFUNC(0x0, IAudioInManager, ListAudioIns),
                SFUNC(0x1, IAudioInManager, OpenAudioIn),
                SFUNC(0x2, IAudioInManager, ListAudioIns),
                SFUNC(0x3, IAudioInManager, OpenAudioIn),
                SFUNC(0x4, IAudioInManager, ListAudioIns),
                SFUNC(0x5, IAudioInManager, OpenAudioInProtocolSpecified)
            )
        };
    }
}
