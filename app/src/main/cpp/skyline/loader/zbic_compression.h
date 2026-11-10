// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Eden Emulator Project (https://git.eden-emu.dev/eden-emu/eden)

#pragma once

#include <span>
#include <common/base.h>

namespace skyline::loader {
    /**
     * @brief Decompresses a segment of an NSO that uses ZBIC compression (the NSO flag bit 7), the format of the newer SDKs (22.x): Zstandard with a different magic number
     * @return The size of the decompressed data or -1 on failure
     * @note This is based on common/zbic_compression.cpp of Eden, the Zstandard implementation with ZBIC support comes from https://github.com/kinnay/zbic
     */
    int DecompressDataZBIC(std::span<u8> dst, std::span<const u8> src);
}
