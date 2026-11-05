// SPDX-License-Identifier: MPL-2.0
// The part of Eden's Host1x class that the codec files use: reading the memory of the game and the queue of decoded frames

#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <common.h>
#include "eden/video_core/host1x/ffmpeg.h"

namespace Tegra::Host1x {
    /**
     * @brief Reads and writes the memory that the engines address through the SMMU of the SoC
     */
    class GmmuManager {
      public:
        const skyline::DeviceState *state{};

        void ReadBlock(u64 address, void *destination, size_t size) const;
        void WriteBlock(u64 address, const void *source, size_t size) const;
    };

    /**
     * @brief The decoded frames the video decoder has produced and the compositor has not taken yet
     * @note The frames are in the order they are shown, which is the only order the decoder here produces
     */
    class FrameQueue {
      public:
        void Open(s32) {}

        void Close(s32) {
            std::scoped_lock lock{mutex};
            frames.clear();
        }

        s32 VicFindNvdecFdFromOffset(u64) { return 0; }

        void PushPresentOrder(s32, u64, std::shared_ptr<FFmpeg::Frame> &&frame) {
            std::scoped_lock lock{mutex};
            if (frames.size() >= MaxQueuedFrames)
                frames.pop_front();
            frames.push_back(std::move(frame));
        }

        void PushDecodeOrder(s32 fd, u64 offset, std::shared_ptr<FFmpeg::Frame> &&frame) {
            PushPresentOrder(fd, offset, std::move(frame));
        }

        std::shared_ptr<FFmpeg::Frame> GetFrame(s32, u64) {
            std::scoped_lock lock{mutex};
            if (frames.empty())
                return {};
            auto frame{std::move(frames.front())};
            frames.pop_front();
            return frame;
        }

      private:
        static constexpr size_t MaxQueuedFrames{100};
        std::mutex mutex;
        std::deque<std::shared_ptr<FFmpeg::Frame>> frames;
    };

    class Host1x {
      public:
        GmmuManager gmmu_manager;
        FrameQueue frame_queue;

        /**
         * @return The one instance every video engine of the process shares (the decoder puts frames in the queue and the compositor takes them)
         */
        static Host1x &Get(const skyline::DeviceState &state) {
            static Host1x instance;
            instance.gmmu_manager.state = &state;
            return instance;
        }
    };
}
