// SPDX-License-Identifier: MPL-2.0

#include <common.h>
#include "ffmpeg_check.h"

#ifdef MANDACARU_VIDEO
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

#ifdef MANDACARU_VIDEO
// The H.264 decoder of FFmpeg 7.1 calls this function (to free the film grain data of AV1) from code that is built without AV1, but the function is only built together with the AV1 decoder, which makes the link fail. Nothing here ever has that data, so an empty function is enough. It is weak so a build of FFmpeg that has the real one is not affected
extern "C" __attribute__((weak)) void ff_aom_uninit_film_grain_params(void *) {}
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
