// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <atomic>
#include "vic.h"

namespace skyline::soc::host1x {
    VicClass::VicClass(const DeviceState &state, std::function<void()> opDoneCallback)
        : state(state), opDoneCallback(std::move(opDoneCallback)) {}

    void VicClass::CallMethod(u32 method, u32 argument) {
        // The VIC converts decoded video frames to RGB, it isn't implemented either (see NvDecClass), only the first call is logged as a warning
        static std::atomic<bool> reported{};
        if (!reported.exchange(true))
            LOGW("The game is using the VIC video compositor (first method: 0x{:X}, argument: 0x{:X}), it is not implemented so videos and cutscenes will not be shown", method, argument);
        else
            LOGD("Unknown VIC class method called: 0x{:X} argument: 0x{:X}", method, argument);
    }
}
