// SPDX-License-Identifier: MPL-2.0
// The interface of Eden's video_core/host1x/ffmpeg.h that the codec files use, with a software only decoder (libavcodec) behind it

#pragma once

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include "eden/common/common_types.h"
#include "eden/video_core/host1x/nvdec_common.h"

namespace FFmpeg {
    enum class PixelFormat {
        Unknown,
        Yuv420P, //!< Separate planes for luma, U and V
        Nv12, //!< A plane for luma and a plane with U and V interleaved
    };

    /**
     * @brief A decoded picture, its planes are copies so nothing depends on the lifetime of the decoder
     */
    class Frame {
      public:
        s32 width{};
        s32 height{};
        PixelFormat format{PixelFormat::Unknown};
        std::array<std::vector<u8>, 3> planes;
        std::array<s32, 3> strides{};

        s32 GetWidth() const { return width; }
        s32 GetHeight() const { return height; }
        PixelFormat GetPixelFormat() const { return format; }
        s32 GetStride(int plane) const { return strides[static_cast<size_t>(plane)]; }
        const u8 *GetPlane(int plane) const { return planes[static_cast<size_t>(plane)].data(); }
    };

    struct FrameOffsets {
        bool interlaced{};
        bool hidden{};
        u64 luma{};
        u64 luma_bottom{};
    };

    struct FrameDimensions {
        s32 width{};
        s32 height{};
    };

    struct DecoderState; //!< Holds the libavcodec objects, only the implementation file knows them

    /**
     * @brief Decodes the bitstreams that are put together from what the video decoder of the console was given
     */
    class DecodeApi {
      public:
        DecodeApi();
        ~DecodeApi();
        DecodeApi(const DecodeApi &) = delete;
        DecodeApi &operator=(const DecodeApi &) = delete;

        /**
         * @return If the decoder for the codec could be opened, false also when the emulator was built without video decoding
         */
        bool Initialize(Tegra::Host1x::NvdecCommon::VideoCodec codec);
        void Reset();

        /**
         * @brief Frames are always handed out in the order they should be shown
         */
        bool UsingDecodeOrder() const { return false; }

        bool SendPacket(std::span<const u8> packet_data, const FrameOffsets &offsets, std::optional<FrameDimensions> dimensions = std::nullopt);

        struct DecodedFrame {
            std::shared_ptr<Frame> frame;
            FrameOffsets offsets;
        };

        std::optional<DecodedFrame> ReceiveFrame();

      private:
        std::unique_ptr<DecoderState> state;
        std::deque<FrameOffsets> pendingOffsets;
    };
}
