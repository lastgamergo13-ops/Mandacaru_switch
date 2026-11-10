// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)
// Copyright © 2026 Mandacaru Contributors

#include <audio.h>
#include <jvm.h>
#include <audio_core/audio_in_manager.h>
#include <audio_core/core/hle/service/audio/errors.h>
#include <kernel/types/KProcess.h>
#include "IAudioInManager.h"
#include "IAudioIn.h"

namespace skyline::service::audio {
    IAudioInManager::IAudioInManager(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

    Result IAudioInManager::ListAudioIns(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        std::vector<AudioCore::AudioRenderer::AudioDevice::AudioDeviceName> names;
        u32 count{};
        try {
            count = state.audio->audioInManager->GetDeviceNames(names, static_cast<u32>(request.outputBuf.at(0).size() / sizeof(names[0])), true);
        } catch (const std::exception &e) {
            LOGW("Cannot list the audio inputs: {}", e.what()); // A host that fails to enumerate its inputs has none for the guest either
            names.clear();
        }

        // Only the first name is written, the manager never reports more than one audio input
        std::memset(request.outputBuf.at(0).data(), 0, request.outputBuf.at(0).size());
        if (count && !names.empty())
            std::memcpy(request.outputBuf.at(0).data(), names[0].name.data(), std::min(names[0].name.size(), request.outputBuf.at(0).size()));

        response.Push<u32>(count);
        return {};
    }

    /**
     * @brief The part of #OpenAudioIn and #OpenAudioInProtocolSpecified that is the same
     */
    static Result OpenAudioInImpl(const DeviceState &state, ServiceManager &manager, type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response, bool protocolSpecified, const std::array<u32, 2> &protocol, const AudioCore::AudioIn::AudioInParameter &parameters, u64 appletResourceUserId) {
        if (request.inputBuf.empty() || request.outputBuf.empty() || request.copyHandles.empty()) {
            LOGW("Invalid buffers or no process handle to open an Audio In");
            return Result{Service::Audio::ResultInvalidHandle};
        }

        std::string_view deviceName{request.inputBuf.at(0).as_string(true)};
        auto handle{request.copyHandles.at(0)};

        // The microphone can only be captured with the permission of the user, which is asked for when a game opens an audio input (and not before). If it isn't given the game gets silence
        if (!state.jvm->RequestMicrophonePermission())
            LOGW("The microphone isn't allowed, the game will capture silence");

        auto &audioInManager{*state.audio->audioInManager};
        if (auto result{audioInManager.LinkToManager()}; result.IsError()) {
            LOGW("Failed to link Audio In to manager");
            return Result{result};
        }

        size_t sessionId{};
        if (auto result{audioInManager.AcquireSessionId(sessionId)}; result.IsError()) {
            LOGW("Failed to acquire audio session");
            return Result{result};
        }

        auto audioIn{std::make_shared<IAudioIn>(state, manager, sessionId, deviceName, parameters, handle, appletResourceUserId)};
        manager.RegisterService(audioIn, session, response);

        audioInManager.sessions[sessionId] = audioIn->impl;
        audioInManager.applet_resource_user_ids[sessionId] = appletResourceUserId;

        auto &outSystem{audioIn->impl->GetSystem()};
        response.Push<AudioCore::AudioIn::AudioInParameterInternal>({
            .sample_rate = outSystem.GetSampleRate(),
            .channel_count = outSystem.GetChannelCount(),
            .sample_format = static_cast<u32>(outSystem.GetSampleFormat()),
            .state = static_cast<u32>(outSystem.GetState())
        });

        std::memset(request.outputBuf.at(0).data(), 0, request.outputBuf.at(0).size());

        // The name that is returned is the one of the device, except when the protocol wasn't specified where it tells whether it's USB audio class
        std::string outName{outSystem.GetName()};
        if (!protocolSpecified || (protocol[0] == 0 && protocol[1] == 0))
            outName = outSystem.IsUac() ? constant::UacAudioInName : constant::DefaultAudioInName;
        request.outputBuf.at(0).copy_from(outName);

        return {};
    }

    Result IAudioInManager::OpenAudioIn(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto parameters{request.Pop<AudioCore::AudioIn::AudioInParameter>()};
        auto appletResourceUserId{request.Pop<u64>()};

        return OpenAudioInImpl(state, manager, session, request, response, false, {}, parameters, appletResourceUserId);
    }

    Result IAudioInManager::OpenAudioInProtocolSpecified(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto protocol{request.Pop<std::array<u32, 2>>()};
        auto parameters{request.Pop<AudioCore::AudioIn::AudioInParameter>()};
        auto appletResourceUserId{request.Pop<u64>()};

        return OpenAudioInImpl(state, manager, session, request, response, true, protocol, parameters, appletResourceUserId);
    }
}
