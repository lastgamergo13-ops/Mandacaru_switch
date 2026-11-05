// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <atomic>
#include <cstddef>
#include "eden/video_core/host1x/host1x.h"
#include "nvdec.h"

namespace skyline::soc::host1x {
    using namespace Tegra::Host1x;

    /**
     * @brief The index of a register in the register array, the methods the game calls are numbered in units of 8 bytes
     */
    #define NVDEC_REG_INDEX(field_name) (offsetof(NvdecCommon::NvdecRegisters, field_name) / sizeof(u64))

    static std::atomic<s32> NextDecoderId{};

    NvDecClass::NvDecClass(const DeviceState &state, std::function<void()> opDoneCallback)
        : state(state), opDoneCallback(std::move(opDoneCallback)), id(NextDecoderId++) {
        Host1x::Get(state).frame_queue.Open(id);
    }

    NvDecClass::~NvDecClass() {
        Host1x::Get(state).frame_queue.Close(id);
    }

    void NvDecClass::CallMethod(u32 method, u32 argument) {
        if (method >= regs.reg_array.size()) {
            LOGW("NVDEC method outside of the registers: 0x{:X} (argument 0x{:X})", method, argument);
            return;
        }

        regs.reg_array[method] = argument;

        switch (method) {
            case NVDEC_REG_INDEX(set_codec_id):
                CreateDecoder(static_cast<NvdecCommon::VideoCodec>(argument));
                break;
            case NVDEC_REG_INDEX(execute):
                Execute();
                break;
            default:
                break;
        }
    }

    void NvDecClass::CreateDecoder(NvdecCommon::VideoCodec codec) {
        if (!std::holds_alternative<std::monostate>(decoder))
            return;

        auto &host1x{Host1x::Get(state)};
        switch (codec) {
            case NvdecCommon::VideoCodec::H264:
                decoder.emplace<Tegra::Decoders::H264>(host1x, regs, id);
                break;
            case NvdecCommon::VideoCodec::VP8:
                decoder.emplace<Tegra::Decoders::VP8>(host1x, regs, id);
                break;
            case NvdecCommon::VideoCodec::VP9:
                decoder.emplace<Tegra::Decoders::VP9>(host1x, regs, id);
                break;
            default:
                if (!unsupportedCodecReported) {
                    unsupportedCodecReported = true;
                    LOGE("The game uses a video codec that isn't supported: {}", static_cast<int>(codec));
                }
                return;
        }

        LOGI("Created the video decoder for codec {} (id {})", static_cast<int>(codec), id);
    }

    void NvDecClass::Execute() {
        // A frame that can't be decoded is skipped, the game goes on and the syncpoint of the command is still signalled
        try {
            if (auto *h264{std::get_if<Tegra::Decoders::H264>(&decoder)})
                h264->Decode();
            else if (auto *vp8{std::get_if<Tegra::Decoders::VP8>(&decoder)})
                vp8->Decode();
            else if (auto *vp9{std::get_if<Tegra::Decoders::VP9>(&decoder)})
                vp9->Decode();
            else
                LOGE("NVDEC was executed without a decoder");
        } catch (const std::exception &e) {
            LOGE("Failed to decode a video frame: {}", e.what());
        }
    }
}
