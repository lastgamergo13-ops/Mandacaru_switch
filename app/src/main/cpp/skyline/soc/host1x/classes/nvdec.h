// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <variant>
#include <common.h>
#include "eden/video_core/host1x/nvdec_common.h"
#include "eden/video_core/host1x/codecs/h264.h"
#include "eden/video_core/host1x/codecs/vp8.h"
#include "eden/video_core/host1x/codecs/vp9.h"

namespace skyline::soc::host1x {
    /**
     * @brief The NVDEC Host1x class implements hardware accelerated video decoding
     * @details The game fills the registers (the codec, the addresses of the bitstream and of the description of the picture) and executes: the picture is described in the format of the console's decoder, which the codec classes turn into a bitstream that FFmpeg understands. The decoded frames are put in a queue that the VIC takes them from
     * @note The codec classes and the register layout are from Eden (video_core/host1x), see the copyright notices of those files
     */
    class NvDecClass {
      private:
        const DeviceState &state;
        std::function<void()> opDoneCallback;
        Tegra::Host1x::NvdecCommon::NvdecRegisters regs{};
        std::variant<std::monostate, Tegra::Decoders::H264, Tegra::Decoders::VP8, Tegra::Decoders::VP9> decoder;
        s32 id;
        bool unsupportedCodecReported{};

        void CreateDecoder(Tegra::Host1x::NvdecCommon::VideoCodec codec);

        void Execute();

      public:
        NvDecClass(const DeviceState &state, std::function<void()> opDoneCallback);

        ~NvDecClass();

        void CallMethod(u32 method, u32 argument);
    };
}
