// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)
// The conversion of the frames follows the VIC of Eden (video_core/host1x/vic.cpp), see the notice in video/eden

#pragma once

#include <vector>
#include <common.h>
#include "eden/video_core/host1x/vic_types.h"
#include "eden/video_core/host1x/ffmpeg.h"

namespace skyline::soc::host1x {
    /**
     * @brief The VIC Host1x class composes the video frames that the decoder produced into the surface that the game shows
     * @details The game describes in a configuration structure which frame goes where, how its colors are converted (the matrix that turns YUV into RGB) and which format and layout the output has. Only the common case is handled: progressive frames of the 4:2:0 formats written as 8 bit RGB, in pitch-linear or block-linear layout
     */
    class VicClass {
      private:
        const DeviceState &state;
        std::function<void()> opDoneCallback;
        Tegra::Host1x::VicRegisters regs{};
        std::vector<Tegra::Host1x::Pixel> slotSurface; //!< The frame of a slot as 10 bit (the 8 bit value shifted left by 2) pixels of the slot's size
        std::vector<Tegra::Host1x::Pixel> outputSurface; //!< What the slots were composed into, it is kept so a frame that isn't there yet shows the last one
        bool hasDecodedFrame{};
        bool unsupportedFormatReported{};

        void Execute();

        /**
         * @brief Puts the frame of a slot in slotSurface
         */
        void ReadSlot(const Tegra::Host1x::SlotStruct &slot, const FFmpeg::Frame &frame);

        /**
         * @brief Converts the colors of slotSurface and puts the result in the rectangle of the output surface that the slot covers
         */
        void Blend(const Tegra::Host1x::ConfigStruct &config, const Tegra::Host1x::SlotStruct &slot);

        /**
         * @brief Writes the output surface to the memory of the game as 8 bit RGB
         */
        void WriteRgb(const Tegra::Host1x::OutputSurfaceConfig &outputConfig, Tegra::Host1x::VideoPixelFormat format);

      public:
        VicClass(const DeviceState &state, std::function<void()> opDoneCallback);

        void CallMethod(u32 method, u32 argument);
    };
}
