// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <atomic>
#include "nvdec.h"

namespace skyline::soc::host1x {
    NvDecClass::NvDecClass(std::function<void()> opDoneCallback)
        : opDoneCallback(std::move(opDoneCallback)) {}

    void NvDecClass::CallMethod(u32 method, u32 argument) {
        // Video decoding isn't implemented, the methods are only logged: once as a warning so a log shows right away that the game wanted to play a video (cutscenes stay empty), and the rest at debug level
        static std::atomic<bool> reported{};
        if (!reported.exchange(true))
            LOGW("The game is using the NVDEC video decoder (first method: 0x{:X}, argument: 0x{:X}), video decoding is not implemented so videos and cutscenes will not be shown", method, argument);
        else
            LOGD("Unknown NVDEC class method called: 0x{:X} argument: 0x{:X}", method, argument);
    }
}
