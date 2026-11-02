// SPDX-License-Identifier: MPL-2.0

#include <common.h>
#include "ffmpeg_check.h"

#ifdef MANDACARU_VIDEO
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

namespace skyline::soc::host1x::video {
    void LogFfmpegSupport() {
#ifdef MANDACARU_VIDEO
        unsigned version{avcodec_version()};
        LOGI("FFmpeg libavcodec {}.{}.{} is linked, decoders: H.264 {}, VP8 {}, VP9 {}", version >> 16, (version >> 8) & 0xFF, version & 0xFF,
             avcodec_find_decoder(AV_CODEC_ID_H264) != nullptr, avcodec_find_decoder(AV_CODEC_ID_VP8) != nullptr, avcodec_find_decoder(AV_CODEC_ID_VP9) != nullptr);
#else
        LOGI("This build has no FFmpeg: videos and cutscenes can't be decoded");
#endif
    }
}
