#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ZSTD_DCtx_s ZSTD_DCtx;
ZSTD_DCtx* ZSTD_createDCtx(void);
size_t ZSTD_freeDCtx(ZSTD_DCtx*);
size_t ZSTD_decompressDCtx(ZSTD_DCtx*, void*, size_t, const void*, size_t);
unsigned ZSTD_isError(size_t);
const char* ZSTD_getErrorName(size_t);
#ifdef __cplusplus
}
#endif
