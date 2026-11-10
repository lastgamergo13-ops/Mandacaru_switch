// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2026 Eden Emulator Project (https://git.eden-emu.dev/eden-emu/eden)

#include <cstring>

#include "zbic_compression.h"

// This is a Zstandard fork with support for the ZBIC magic number, it's included as source here with all of its symbols static so they can't clash with the regular Zstandard that's linked for NCZ files
#define ZSTD_ZBIC_SUPPORT 1
#define ZSTDLIB_VISIBLE static
#define ZSTDLIB_HIDDEN static
#define ZSTDERRORLIB_VISIBLE static
#define ZSTDERRORLIB_HIDDEN static
#undef ZSTD_MULTITHREAD

#if defined(__ANDROID__)
#undef _GNU_SOURCE
#endif

#include "zstd.h"
#define g_ZSTD_threading_useless_symbol g_ZSTD_zbic_threading_useless_symbol
#include "zstd.c"
#undef g_ZSTD_threading_useless_symbol

namespace skyline::loader {
    int DecompressDataZBIC(std::span<u8> dst, std::span<const u8> src) {
        if (dst.empty() || src.empty())
            return -1;

        const size_t result{ZSTD_decompress(dst.data(), dst.size(), src.data(), src.size())};
        if (ZSTD_isError(result))
            return -1;

        return static_cast<int>(result);
    }
}
