// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <common.h>

#define ASSERT(cond) do { if (!(cond)) throw skyline::exception("Assertion failed: " #cond); } while (0)
#define ASSERT_MSG(cond, ...) do { if (!(cond)) throw skyline::exception(__VA_ARGS__); } while (0)
#define UNREACHABLE() throw skyline::exception("Unreachable code in the video decoder")
#define UNIMPLEMENTED_MSG(...) LOGW(__VA_ARGS__)
