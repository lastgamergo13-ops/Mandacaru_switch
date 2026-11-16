// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace skyline::input {
    /**
     * @brief Applies a circular deadzone to a stick
     * @param x The X axis of the stick, in the range of an i16
     * @param y The Y axis of the stick, in the range of an i16
     * @param percent The radius of the deadzone in percent of the full range (0 disables it, it's limited to 90)
     * @return The axes after the deadzone: inside of it the stick is at rest (0, 0), outside of it the range starts again from zero at the edge of the deadzone and still reaches the full range, so there's no jump when the stick leaves it. The direction is kept
     */
    inline std::pair<int32_t, int32_t> ApplyCircularDeadzone(int32_t x, int32_t y, uint32_t percent) {
        constexpr double Max{std::numeric_limits<int16_t>::max()};
        const double deadzone{Max * std::min<uint32_t>(percent, 90) / 100.0};
        if (deadzone <= 0.0)
            return {x, y};

        const double magnitude{std::hypot(static_cast<double>(x), static_cast<double>(y))};
        if (magnitude <= deadzone)
            return {0, 0};

        const double scale{std::min(1.0, (magnitude - deadzone) / (Max - deadzone)) * Max / magnitude};
        return {static_cast<int32_t>(std::lround(x * scale)), static_cast<int32_t>(std::lround(y * scale))};
    }
}
