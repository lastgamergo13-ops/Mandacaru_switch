// SPDX-License-Identifier: MPL-2.0
// Copyright © 2026 Mandacaru Contributors
// Copyright © 2022 yuzu Emulator Project (https://github.com/yuzu-emu/)

#include <audio.h>
#include <audio_core/core/hle/service/audio/errors.h>
#include <kernel/types/KProcess.h>
#include "IAudioIn.h"

namespace skyline::service::audio {
    IAudioIn::IAudioIn(const DeviceState &state, ServiceManager &manager, size_t sessionId,
                       std::string_view deviceName, AudioCore::AudioIn::AudioInParameter parameters,
                       KHandle handle, u64 appletResourceUserId)
        : BaseService{state, manager},
          releaseEvent{std::make_shared<type::KEvent>(state, false)},
          releaseEventWrapper{[releaseEvent = this->releaseEvent]() { releaseEvent->Signal(); },
                              [releaseEvent = this->releaseEvent]() { releaseEvent->ResetSignal(); }},
          impl{std::make_shared<AudioCore::AudioIn::In>(state.audio->audioSystem, *state.audio->audioInManager, &releaseEventWrapper, sessionId)} {
        if (impl->GetSystem().Initialize(std::string{deviceName}, parameters, handle, appletResourceUserId).IsError())
            LOGW("Failed to initialise Audio In");
    }

    IAudioIn::~IAudioIn() {
        impl->Free();
    }

    Result IAudioIn::GetAudioInState(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push(static_cast<u32>(impl->GetState()));
        return {};
    }

    Result IAudioIn::Start(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return Result{impl->StartSystem()};
    }

    Result IAudioIn::Stop(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        return Result{impl->StopSystem()};
    }

    Result IAudioIn::AppendAudioInBuffer(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        if (request.inputBuf.empty() || request.inputBuf.at(0).size() < sizeof(AudioCore::AudioIn::AudioInBuffer)) {
            LOGW("The buffer of AppendAudioInBuffer is too small for an AudioInBuffer");
            return Result{Service::Audio::ResultInsufficientBuffer};
        }

        const auto &buffer{request.inputBuf.at(0).as<AudioCore::AudioIn::AudioInBuffer>()};
        auto tag{request.Pop<u64>()};

        return Result{impl->AppendBuffer(buffer, tag)};
    }

    Result IAudioIn::RegisterBufferEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto handle{state.process->InsertItem(releaseEvent)};
        LOGD("Buffer Release Event Handle: 0x{:X}", handle);
        response.copyHandles.push_back(handle);
        return {};
    }

    Result IAudioIn::GetReleasedAudioInBuffers(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto maxCount{request.outputBuf.at(0).size() >> 3};

        std::vector<u64> releasedBuffers(maxCount);
        auto count{impl->GetReleasedBuffers(releasedBuffers)};

        request.outputBuf.at(0).copy_from(releasedBuffers);
        response.Push<u32>(count);
        return {};
    }

    Result IAudioIn::ContainsAudioInBuffer(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto tag{request.Pop<u64>()};
        response.Push<u8>(impl->ContainsAudioBuffer(tag));
        return {};
    }

    Result IAudioIn::GetAudioInBufferCount(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u32>(impl->GetBufferCount());
        return {};
    }

    Result IAudioIn::SetDeviceGain(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        impl->SetVolume(request.Pop<float>());
        return {};
    }

    Result IAudioIn::GetDeviceGain(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<float>(impl->GetVolume());
        return {};
    }

    Result IAudioIn::FlushAudioInBuffers(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u8>(impl->FlushAudioInBuffers());
        return {};
    }
}
