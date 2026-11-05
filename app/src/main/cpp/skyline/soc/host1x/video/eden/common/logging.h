// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <common.h>

// Eden's log macros take a category first, it is dropped
#define LOG_ERROR(category, ...) LOGE(__VA_ARGS__)
#define LOG_WARNING(category, ...) LOGW(__VA_ARGS__)
#define LOG_INFO(category, ...) LOGI(__VA_ARGS__)
#define LOG_DEBUG(category, ...) LOGD(__VA_ARGS__)
#define LOG_TRACE(category, ...) LOGD(__VA_ARGS__)
