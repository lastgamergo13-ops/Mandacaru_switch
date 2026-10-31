// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <services/serviceman.h>

#include "IHardwareOpusDecoderManager.h"
#include "IHardwareOpusDecoder.h"

namespace skyline::service::codec {
    static u32 CalculateBufferSize(i32 sampleRate, i32 channelCount, i32 useLargerFrameSize = 0) {
        u32 requiredSize{static_cast<u32>(opus_decoder_get_size(channelCount))};
        requiredSize += MaxInputBufferSize + CalculateOutBufferSize(sampleRate, channelCount, useLargerFrameSize ? MaxFrameSizeEx : MaxFrameSizeNormal);
        return requiredSize;
    }

    Result IHardwareOpusDecoderManager::OpenHardwareOpusDecoder(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 sampleRate{request.Pop<i32>()};
        i32 channelCount{request.Pop<i32>()};
        u32 workBufferSize{request.Pop<u32>()};
        KHandle workBuffer{request.copyHandles.at(0)};

        LOGD("Creating Opus decoder: Sample rate: {}, Channel count: {}, Work buffer handle: 0x{:X} (Size: 0x{:X})", sampleRate, channelCount, workBuffer, workBufferSize);

        manager.RegisterService(std::make_shared<IHardwareOpusDecoder>(state, manager, sampleRate, channelCount, workBufferSize, workBuffer), session, response);
        return {};
    }

    Result IHardwareOpusDecoderManager::GetWorkBufferSize(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 sampleRate{request.Pop<i32>()};
        i32 channelCount{request.Pop<i32>()};

        response.Push<u32>(CalculateBufferSize(sampleRate, channelCount));
        return {};
    }

    Result IHardwareOpusDecoderManager::OpenHardwareOpusDecoderEx(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 sampleRate{request.Pop<i32>()};
        i32 channelCount{request.Pop<i32>()};
        i32 useLargerFrameSize{request.Pop<i32>()};
        request.Pop<i32>(); // Just padding
        u32 workBufferSize{request.Pop<u32>()};
        KHandle workBuffer{request.copyHandles.at(0)};

        LOGD("Creating Opus decoder: Sample rate: {}, Channel count: {}, Work buffer handle: 0x{:X} (Size: 0x{:X})", sampleRate, channelCount, workBuffer, workBufferSize);

        manager.RegisterService(std::make_shared<IHardwareOpusDecoder>(state, manager, sampleRate, channelCount, workBufferSize, workBuffer, useLargerFrameSize), session, response);
        return {};
    }

    /**
     * @return The parameters of a multi-stream decoder as the guest sent them (with or without the larger frame size flag), throws if they can't describe a decoder
     */
    static MultiStreamParametersEx ReadMultiStreamParameters(ipc::IpcRequest &request, bool extended) {
        auto buffer{request.inputBuf.at(0)};
        MultiStreamParametersEx parameters{};

        if (extended) {
            if (buffer.size() < sizeof(MultiStreamParametersEx))
                throw exception("Multi-stream parameters buffer is too small: 0x{:X}", buffer.size());
            parameters = buffer.as<MultiStreamParametersEx>();
        } else {
            if (buffer.size() < sizeof(MultiStreamParameters))
                throw exception("Multi-stream parameters buffer is too small: 0x{:X}", buffer.size());
            auto &legacy{buffer.as<MultiStreamParameters>()};
            parameters = MultiStreamParametersEx{legacy.sampleRate, legacy.channelCount, legacy.streamCount, legacy.stereoStreamCount, 0, {}, legacy.mappings};
        }

        bool validRate{parameters.sampleRate == 8000 || parameters.sampleRate == 12000 || parameters.sampleRate == 16000 || parameters.sampleRate == 24000 || parameters.sampleRate == 48000};
        if (!validRate || parameters.channelCount < 1 || parameters.channelCount > 255 || parameters.streamCount < 1 || parameters.stereoStreamCount < 0 ||
            parameters.stereoStreamCount > parameters.streamCount || parameters.streamCount + parameters.stereoStreamCount > parameters.channelCount)
            throw exception("Invalid Opus multi-stream parameters: rate {}, channels {}, streams {}, stereo streams {}", parameters.sampleRate, parameters.channelCount, parameters.streamCount, parameters.stereoStreamCount);

        return parameters;
    }

    /**
     * @return The size of the work buffer of a multi-stream decoder: the libopus decoder, one input packet per stream and one output frame
     */
    static u32 CalculateMultiStreamBufferSize(const MultiStreamParametersEx &parameters) {
        u32 requiredSize{static_cast<u32>(opus_multistream_decoder_get_size(parameters.streamCount, parameters.stereoStreamCount))};
        requiredSize += util::AlignUp(1500 * static_cast<u32>(parameters.streamCount), 0x40);
        requiredSize += CalculateOutBufferSize(parameters.sampleRate, parameters.channelCount, parameters.useLargeFrameSize ? MaxFrameSizeEx : MaxFrameSizeNormal);
        return requiredSize;
    }

    Result IHardwareOpusDecoderManager::OpenHardwareOpusDecoderForMultiStream(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        u32 workBufferSize{request.Pop<u32>()};
        KHandle workBuffer{request.copyHandles.at(0)};
        auto parameters{ReadMultiStreamParameters(request, false)};

        LOGD("Creating Opus multi-stream decoder: Sample rate: {}, Channel count: {}, Streams: {} ({} stereo), Work buffer handle: 0x{:X} (Size: 0x{:X})", parameters.sampleRate, parameters.channelCount, parameters.streamCount, parameters.stereoStreamCount, workBuffer, workBufferSize);

        manager.RegisterService(std::make_shared<IHardwareOpusDecoder>(state, manager, parameters, workBufferSize, workBuffer), session, response);
        return {};
    }

    Result IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStream(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto parameters{ReadMultiStreamParameters(request, false)};

        response.Push<u32>(CalculateMultiStreamBufferSize(parameters));
        return {};
    }

    Result IHardwareOpusDecoderManager::OpenHardwareOpusDecoderForMultiStreamEx(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        u32 workBufferSize{request.Pop<u32>()};
        KHandle workBuffer{request.copyHandles.at(0)};
        auto parameters{ReadMultiStreamParameters(request, true)};

        LOGD("Creating Opus multi-stream decoder: Sample rate: {}, Channel count: {}, Streams: {} ({} stereo), Work buffer handle: 0x{:X} (Size: 0x{:X})", parameters.sampleRate, parameters.channelCount, parameters.streamCount, parameters.stereoStreamCount, workBuffer, workBufferSize);

        manager.RegisterService(std::make_shared<IHardwareOpusDecoder>(state, manager, parameters, workBufferSize, workBuffer), session, response);
        return {};
    }

    Result IHardwareOpusDecoderManager::GetWorkBufferSizeForMultiStreamEx(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto parameters{ReadMultiStreamParameters(request, true)};

        response.Push<u32>(CalculateMultiStreamBufferSize(parameters));
        return {};
    }

    Result IHardwareOpusDecoderManager::GetWorkBufferSizeEx(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        i32 sampleRate{request.Pop<i32>()};
        i32 channelCount{request.Pop<i32>()};
        i32 useLargerFrameSize{request.Pop<i32>()};
        request.Pop<i32>(); // Just padding

        response.Push<u32>(CalculateBufferSize(sampleRate, channelCount, useLargerFrameSize));
        return {};
    }
}
