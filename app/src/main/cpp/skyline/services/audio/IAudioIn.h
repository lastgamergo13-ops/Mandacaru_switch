// SPDX-License-Identifier: MPL-2.0
// Copyright © 2026 Mandacaru Contributors
// Copyright © 2022 yuzu Emulator Project (https://github.com/yuzu-emu/)

#pragma once

#include <audio_core/in/audio_in_system.h>
#include <audio_core/in/audio_in.h>
#include <audio_core/core/hle/kernel/k_event.h>
#include <services/serviceman.h>

namespace skyline::service::audio {
    /**
     * @brief IAudioIn is an audio input (the microphone) that games capture sound from, it's returned by #IAudioInManager
     * @url https://switchbrew.org/wiki/Audio_services#IAudioIn
     */
    class IAudioIn : public BaseService {
      private:
        std::shared_ptr<type::KEvent> releaseEvent;
        KernelShim::KEvent releaseEventWrapper;

      public:
        std::shared_ptr<AudioCore::AudioIn::In> impl;

        IAudioIn(const DeviceState &state, ServiceManager &manager, size_t sessionId,
                 std::string_view deviceName, AudioCore::AudioIn::AudioInParameter parameters,
                 KHandle handle, u64 appletResourceUserId);

        ~IAudioIn();

        /**
         * @url https://switchbrew.org/wiki/Audio_services#GetAudioInState
         */
        Result GetAudioInState(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Audio_services#StartAudioIn
         */
        Result Start(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Audio_services#StopAudioIn
         */
        Result Stop(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Appends a buffer that the captured audio is written to (it also serves AppendUacInBuffer and the Auto versions)
         * @url https://switchbrew.org/wiki/Audio_services#AppendAudioInBuffer
         */
        Result AppendAudioInBuffer(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns a handle to the event that is signalled when a buffer is released
         * @url https://switchbrew.org/wiki/Audio_services#RegisterBufferEvent
         */
        Result RegisterBufferEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @brief Returns the tags of the released buffers (it also serves the Auto version)
         * @url https://switchbrew.org/wiki/Audio_services#GetReleasedAudioInBuffers
         */
        Result GetReleasedAudioInBuffers(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Audio_services#ContainsAudioInBuffer
         */
        Result ContainsAudioInBuffer(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Audio_services#GetAudioInBufferCount
         */
        Result GetAudioInBufferCount(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Audio_services#SetAudioInDeviceGain
         */
        Result SetDeviceGain(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Audio_services#GetAudioInDeviceGain
         */
        Result GetDeviceGain(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        /**
         * @url https://switchbrew.org/wiki/Audio_services#FlushAudioInBuffers
         */
        Result FlushAudioInBuffers(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response);

        SERVICE_DECL(
            SFUNC(0x0, IAudioIn, GetAudioInState),
            SFUNC(0x1, IAudioIn, Start),
            SFUNC(0x2, IAudioIn, Stop),
            SFUNC(0x3, IAudioIn, AppendAudioInBuffer),
            SFUNC(0x4, IAudioIn, RegisterBufferEvent),
            SFUNC(0x5, IAudioIn, GetReleasedAudioInBuffers),
            SFUNC(0x6, IAudioIn, ContainsAudioInBuffer),
            SFUNC(0x7, IAudioIn, AppendAudioInBuffer),
            SFUNC(0x8, IAudioIn, AppendAudioInBuffer),
            SFUNC(0x9, IAudioIn, GetReleasedAudioInBuffers),
            SFUNC(0xA, IAudioIn, AppendAudioInBuffer),
            SFUNC(0xB, IAudioIn, GetAudioInBufferCount),
            SFUNC(0xC, IAudioIn, SetDeviceGain),
            SFUNC(0xD, IAudioIn, GetDeviceGain),
            SFUNC(0xE, IAudioIn, FlushAudioInBuffers)
        )
    };
}
