// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <cstring>
#include <common.h>
#include "eden/video_core/host1x/ffmpeg.h"

#ifdef MANDACARU_VIDEO
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}
#endif

namespace FFmpeg {
#ifdef MANDACARU_VIDEO
    struct DecoderState {
        const AVCodec *codec{};
        AVCodecContext *context{};
        AVPacket *packet{};
        AVFrame *frame{};
        std::vector<u8> buffer; //!< The packet with the padding libavcodec needs after it

        ~DecoderState() {
            av_frame_free(&frame);
            av_packet_free(&packet);
            avcodec_free_context(&context);
        }
    };

    DecodeApi::DecodeApi() = default;
    DecodeApi::~DecodeApi() = default;

    bool DecodeApi::Initialize(Tegra::Host1x::NvdecCommon::VideoCodec codec) {
        using Tegra::Host1x::NvdecCommon::VideoCodec;
        AVCodecID id;
        switch (codec) {
            case VideoCodec::H264:
                id = AV_CODEC_ID_H264;
                break;
            case VideoCodec::VP8:
                id = AV_CODEC_ID_VP8;
                break;
            case VideoCodec::VP9:
                id = AV_CODEC_ID_VP9;
                break;
            default:
                LOGE("The video codec {} isn't supported", static_cast<int>(codec));
                return false;
        }

        auto newState{std::make_unique<DecoderState>()};
        newState->codec = avcodec_find_decoder(id);
        if (!newState->codec) {
            LOGE("FFmpeg has no decoder for the video codec {}", static_cast<int>(codec));
            return false;
        }

        newState->context = avcodec_alloc_context3(newState->codec);
        newState->packet = av_packet_alloc();
        newState->frame = av_frame_alloc();
        if (!newState->context || !newState->packet || !newState->frame)
            return false;

        // Games decode a few frames ahead of what they show, so the decoder should not hold frames back
        newState->context->thread_count = 2;
        newState->context->thread_type = FF_THREAD_SLICE;
        newState->context->flags |= AV_CODEC_FLAG_LOW_DELAY;

        if (int result{avcodec_open2(newState->context, newState->codec, nullptr)}; result < 0) {
            LOGE("Failed to open the {} decoder: {}", newState->codec->name, result);
            return false;
        }

        state = std::move(newState);
        pendingOffsets.clear();
        LOGI("Video decoder {} is ready", state->codec->name);
        return true;
    }

    void DecodeApi::Reset() {
        if (state && state->context)
            avcodec_flush_buffers(state->context);
        pendingOffsets.clear();
    }

    bool DecodeApi::SendPacket(std::span<const u8> packet_data, const FrameOffsets &offsets, std::optional<FrameDimensions>) {
        if (!state || packet_data.empty())
            return false;

        state->buffer.assign(packet_data.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
        std::memcpy(state->buffer.data(), packet_data.data(), packet_data.size());
        state->packet->data = state->buffer.data();
        state->packet->size = static_cast<int>(packet_data.size());

        int result{avcodec_send_packet(state->context, state->packet)};
        state->packet->data = nullptr;
        state->packet->size = 0;
        if (result < 0 && result != AVERROR(EAGAIN)) {
            LOGW("The video decoder rejected a packet of {} bytes: {}", packet_data.size(), result);
            return false;
        }

        pendingOffsets.push_back(offsets);
        return true;
    }

    std::optional<DecodeApi::DecodedFrame> DecodeApi::ReceiveFrame() {
        if (!state)
            return std::nullopt;

        if (avcodec_receive_frame(state->context, state->frame) < 0)
            return std::nullopt;

        auto frame{std::make_shared<Frame>()};
        frame->width = state->frame->width;
        frame->height = state->frame->height;

        auto copyPlane{[&](size_t plane, const AVFrame *source, int rows, int bytesPerRow) {
            frame->strides[plane] = bytesPerRow;
            frame->planes[plane].resize(static_cast<size_t>(rows) * static_cast<size_t>(bytesPerRow));
            for (int row{}; row < rows; row++)
                std::memcpy(frame->planes[plane].data() + static_cast<size_t>(row) * static_cast<size_t>(bytesPerRow), source->data[plane] + static_cast<ptrdiff_t>(row) * source->linesize[plane], static_cast<size_t>(bytesPerRow));
        }};

        int chromaWidth{(frame->width + 1) / 2}, chromaHeight{(frame->height + 1) / 2};
        switch (state->frame->format) {
            case AV_PIX_FMT_YUV420P:
            case AV_PIX_FMT_YUVJ420P:
                frame->format = PixelFormat::Yuv420P;
                copyPlane(0, state->frame, frame->height, frame->width);
                copyPlane(1, state->frame, chromaHeight, chromaWidth);
                copyPlane(2, state->frame, chromaHeight, chromaWidth);
                break;
            case AV_PIX_FMT_NV12:
                frame->format = PixelFormat::Nv12;
                copyPlane(0, state->frame, frame->height, frame->width);
                copyPlane(1, state->frame, chromaHeight, chromaWidth * 2);
                break;
            default:
                LOGW("The decoded video frame has a pixel format that isn't supported: {}", state->frame->format);
                av_frame_unref(state->frame);
                return std::nullopt;
        }
        av_frame_unref(state->frame);

        FrameOffsets offsets;
        if (!pendingOffsets.empty()) {
            offsets = pendingOffsets.front();
            pendingOffsets.pop_front();
        }
        return DecodedFrame{std::move(frame), offsets};
    }
#else
    // Built without FFmpeg: nothing can be decoded, the video engines then accept the commands and show nothing
    struct DecoderState {};

    DecodeApi::DecodeApi() = default;
    DecodeApi::~DecodeApi() = default;
    bool DecodeApi::Initialize(Tegra::Host1x::NvdecCommon::VideoCodec) { return false; }
    void DecodeApi::Reset() {}
    bool DecodeApi::SendPacket(std::span<const u8>, const FrameOffsets &, std::optional<FrameDimensions>) { return false; }
    std::optional<DecodeApi::DecodedFrame> DecodeApi::ReceiveFrame() { return std::nullopt; }
#endif
}
