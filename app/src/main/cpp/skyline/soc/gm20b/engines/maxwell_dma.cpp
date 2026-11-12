// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)
// Copyright © 2022 yuzu Emulator Project (https://github.com/yuzu-emu/yuzu/)

#include <gpu/interconnect/command_executor.h>
#include <gpu/texture/format.h>
#include <gpu/texture/layout.h>
#include <soc.h>
#include <soc/gm20b/channel.h>
#include <soc/gm20b/gmmu.h>
#include "maxwell_dma.h"

namespace skyline::soc::gm20b::engine {
    MaxwellDma::MaxwellDma(const DeviceState &state, ChannelContext &channelCtx)
        : channelCtx{channelCtx},
          syncpoints{state.soc->host1x.syncpoints},
          interconnect{*state.gpu, channelCtx},
          copyCache() {}

    __attribute__((always_inline)) void MaxwellDma::CallMethod(u32 method, u32 argument) {
        LOGV("Called method in Maxwell DMA: 0x{:X} args: 0x{:X}", method, argument);

        HandleMethod(method, argument);
    }

    void MaxwellDma::HandleMethod(u32 method, u32 argument) {
        registers.raw[method] = argument;

        if (method == ENGINE_OFFSET(launchDma))
            LaunchDma();
    }

    void MaxwellDma::LaunchDma() {
        DmaCopy();

        ReleaseSemaphore();
    }

    namespace {
        /**
         * @return If any part of the translated range isn't backed by host memory (the guest unmapped it or never mapped it)
         */
        bool HasUnmappedRange(const TranslatedAddressRange &mappings) {
            for (const auto &mapping : mappings)
                if (mapping.data() == nullptr)
                    return true;
            return false;
        }

        /**
         * @brief Reads from the GMMU without letting a page fault take the whole emulator down, anything that can't be read is returned as zeroes
         * @note Games (e.g. Stray) can issue DMA copies that touch pages that aren't mapped (anymore), real hardware just reads/ignores those accesses
         */
        void SafeGmmuRead(GMMU &gmmu, u8 *destination, u64 address, size_t size) {
            try {
                gmmu.Read(destination, address, size);
                return;
            } catch (const std::exception &) {}

            // Fall back to page by page so that everything that is mapped still gets copied
            std::memset(destination, 0, size);
            for (size_t offset{}; offset < size;) {
                size_t chunk{std::min<size_t>(GmmuSmallPageSize - ((address + offset) & (GmmuSmallPageSize - 1)), size - offset)};
                try {
                    gmmu.Read(destination + offset, address + offset, chunk);
                } catch (const std::exception &) {
                    std::memset(destination + offset, 0, chunk);
                }
                offset += chunk;
            }
        }

        /**
         * @brief Writes to the GMMU without letting a page fault take the whole emulator down, anything that can't be written is dropped
         */
        void SafeGmmuWrite(GMMU &gmmu, u64 address, u8 *source, size_t size) {
            try {
                gmmu.Write(address, source, size);
                return;
            } catch (const std::exception &) {}

            for (size_t offset{}; offset < size;) {
                size_t chunk{std::min<size_t>(GmmuSmallPageSize - ((address + offset) & (GmmuSmallPageSize - 1)), size - offset)};
                try {
                    gmmu.Write(address + offset, source + offset, chunk);
                } catch (const std::exception &) {}
                offset += chunk;
            }
        }
    }

    void MaxwellDma::DmaCopy() {
        if (registers.launchDma->multiLineEnable) {
            if (registers.launchDma->remapEnable) [[unlikely]] {
                LOGW("Remapped DMA copies are unimplemented!");
                return;
            }

            channelCtx.executor.Submit();

            if (registers.launchDma->srcMemoryLayout == registers.launchDma->dstMemoryLayout) [[unlikely]] {
                // Pitch to Pitch copy
                if (registers.launchDma->srcMemoryLayout == Registers::LaunchDma::MemoryLayout::Pitch) [[likely]] {
                    CopyPitchToPitch();
                } else {
                    LOGW("BlockLinear to BlockLinear DMA copies are unimplemented!");
                }
            } else if (registers.launchDma->srcMemoryLayout == Registers::LaunchDma::MemoryLayout::BlockLinear) {
                CopyBlockLinearToPitch();
            } else [[likely]] {
                CopyPitchToBlockLinear();
            }
        } else {
            // 1D copy
            LOGD("src: 0x{:X} dst: 0x{:X} size: 0x{:X}", u64{*registers.offsetIn}, u64{*registers.offsetOut}, *registers.lineLengthIn);

            size_t dstBpp{registers.launchDma->remapEnable ? static_cast<size_t>(registers.remapComponents->NumDstComponents() * registers.remapComponents->ComponentSize()) : 1};

            auto srcMappings{channelCtx.asCtx->gmmu.TranslateRange(*registers.offsetIn, *registers.lineLengthIn)};
            auto dstMappings{channelCtx.asCtx->gmmu.TranslateRange(*registers.offsetOut, *registers.lineLengthIn * dstBpp)};

            if (registers.launchDma->remapEnable) [[unlikely]] {
                // Remapped buffer clears
                if ((registers.remapComponents->dstX == Registers::RemapComponents::Swizzle::ConstA) &&
                    (registers.remapComponents->dstY == Registers::RemapComponents::Swizzle::ConstA) &&
                    (registers.remapComponents->dstZ == Registers::RemapComponents::Swizzle::ConstA) &&
                    (registers.remapComponents->dstW == Registers::RemapComponents::Swizzle::ConstA) &&
                    (registers.remapComponents->ComponentSize() == 4)) {
                    for (auto mapping : dstMappings)
                        interconnect.Clear(mapping, *registers.remapConstA);
                } else {
                    LOGW("Remapped DMA copies are unimplemented!");
                }
            } else {
                // When exactly one side of a 1D copy is block linear memory (a kind other than pitch), the data of it is swizzled inside the GOBs (blocks of 512 bytes), so the copy has to move it 16 bytes at a time between the linear and the swizzled address
                u64 srcAddress{*registers.offsetIn}, dstAddress{*registers.offsetOut};
                u32 length{*registers.lineLengthIn};
                // Memory that wasn't mapped with a kind is treated as linear so copies through it stay as they were
                auto isLinear{[](u8 kind) { return pte_kind::IsPitch(kind) || kind == pte_kind::Invalid; }};
                bool srcPitch{isLinear(channelCtx.asCtx->kinds.Get(srcAddress))}, dstPitch{isLinear(channelCtx.asCtx->kinds.Get(dstAddress))};

                if (srcPitch != dstPitch) {
                    if (length % 16 != 0 || srcAddress % 16 != 0 || dstAddress % 16 != 0) [[unlikely]] {
                        LOGW("Swizzled 1D DMA copies that aren't aligned to 16 bytes are unimplemented! src: 0x{:X} dst: 0x{:X} size: 0x{:X}", srcAddress, dstAddress, length);
                        return;
                    }

                    // The address of the same 16 byte chunk in swizzled memory (the bit layout of the GOB of the Maxwell GPU)
                    auto swizzle{[](u64 address) -> u64 {
                        return (address & ~0x1F0ULL) | ((address & 0x40) >> 2) | ((address & 0x10) << 1) | ((address & 0x180) >> 1) | ((address & 0x20) << 3);
                    }};

                    channelCtx.executor.Submit();

                    std::array<u8, 16> chunk;
                    for (u32 offset{}; offset < length; offset += 16) {
                        u64 srcChunk{srcPitch ? srcAddress + offset : swizzle(srcAddress + offset)};
                        u64 dstChunk{dstPitch ? dstAddress + offset : swizzle(dstAddress + offset)};

                        channelCtx.asCtx->gmmu.Read(chunk.data(), srcChunk, chunk.size());
                        channelCtx.asCtx->gmmu.Write(dstChunk, chunk.data(), chunk.size());
                    }
                } else if (srcMappings.size() != 1 || dstMappings.size() != 1) [[unlikely]] {
                    channelCtx.asCtx->gmmu.Copy(u64{*registers.offsetOut}, u64{*registers.offsetIn}, *registers.lineLengthIn);
                } else {
                    interconnect.Copy(dstMappings.front(), srcMappings.front());
                }
            }
        }
    }

    void MaxwellDma::HandleSplitCopy(TranslatedAddressRange srcMappings, TranslatedAddressRange dstMappings, size_t srcSize, size_t dstSize, auto copyCallback) {
        // A range that is split or has unmapped holes can't be accessed through a single host pointer, so it goes through a bounce buffer that is filled/flushed with page fault tolerant accesses
        bool isSrcSplit{srcMappings.size() != 1 || HasUnmappedRange(srcMappings)};
        bool isDstSplit{dstMappings.size() != 1 || HasUnmappedRange(dstMappings)};

        size_t srcOffset{isSrcSplit ? srcSize : 0};
        size_t cacheSize{srcOffset + (isDstSplit ? dstSize : 0)};
        if (copyCache.size() < cacheSize)
            copyCache.resize(cacheSize); // Needs to hold the source and the destination at the same time, sizing it for only one of them overflowed the buffer

        u8 *src{isSrcSplit ? copyCache.data() : srcMappings.front().data()};
        u8 *dst{isDstSplit ? copyCache.data() + srcOffset : dstMappings.front().data()};

        if (isSrcSplit)
            SafeGmmuRead(channelCtx.asCtx->gmmu, src, u64{*registers.offsetIn}, srcSize);

        // If the destination is not entirely filled by the copy (block linear surfaces, or pitch copies where the pitch is larger than the line) we copy it's current state in the cache to prevent clearing of other data.
        if (isDstSplit)
            SafeGmmuRead(channelCtx.asCtx->gmmu, dst, u64{*registers.offsetOut}, dstSize);

        copyCallback(src, dst);

        if (isDstSplit)
            SafeGmmuWrite(channelCtx.asCtx->gmmu, u64{*registers.offsetOut}, dst, dstSize);
    }

    void MaxwellDma::CopyPitchToPitch() {
        auto srcMappings{channelCtx.asCtx->gmmu.TranslateRange(*registers.offsetIn, *registers.pitchIn * *registers.lineCount)};
        auto dstMappings{channelCtx.asCtx->gmmu.TranslateRange(*registers.offsetOut, *registers.pitchOut * *registers.lineCount)};

        if (srcMappings.size() != 1 || dstMappings.size() != 1 || HasUnmappedRange(srcMappings) || HasUnmappedRange(dstMappings)) [[unlikely]] {
            // The bounce buffers have to hold every line of the copy (they used to be sized for a single line which overflowed them for multi line copies)
            size_t lineCount{*registers.lineCount};
            size_t srcSize{lineCount ? (*registers.pitchIn * (lineCount - 1)) + *registers.lineLengthIn : 0};
            size_t dstSize{lineCount ? (*registers.pitchOut * (lineCount - 1)) + *registers.lineLengthIn : 0};

            HandleSplitCopy(srcMappings, dstMappings, srcSize, dstSize, [&](u8 *src, u8 *dst) {
                // Both Linear, copy as is.
                if ((*registers.pitchIn == *registers.pitchOut) && (*registers.pitchIn == *registers.lineLengthIn))
                    std::memcpy(dst, src, *registers.lineLengthIn * *registers.lineCount);
                else
                    for (size_t linesToCopy{*registers.lineCount}, srcCopyOffset{}, dstCopyOffset{}; linesToCopy; --linesToCopy, srcCopyOffset += *registers.pitchIn, dstCopyOffset += *registers.pitchOut)
                        std::memcpy(dst + dstCopyOffset, src + srcCopyOffset, *registers.lineLengthIn);
            });
        } else [[likely]] {
            // Both Linear, copy as is.
            if ((*registers.pitchIn == *registers.pitchOut) && (*registers.pitchIn == *registers.lineLengthIn)) {
                std::memcpy(dstMappings.front().data(), srcMappings.front().data(), *registers.lineLengthIn * *registers.lineCount);
            } else {
                for (size_t linesToCopy{*registers.lineCount}, srcCopyOffset{}, dstCopyOffset{}; linesToCopy; --linesToCopy, srcCopyOffset += *registers.pitchIn, dstCopyOffset += *registers.pitchOut)
                    std::memcpy(dstMappings.front().subspan(dstCopyOffset).data(), srcMappings.front().subspan(srcCopyOffset).data(), *registers.lineLengthIn);
            }
        }
    }

    void MaxwellDma::CopyBlockLinearToPitch() {
        if (registers.srcSurface->blockSize.Width() != 1) [[unlikely]] {
            LOGE("Blocklinear surfaces with a non-one block width are unsupported on the Tegra X1: {}", registers.srcSurface->blockSize.Width());
            return;
        }

        gpu::texture::Dimensions srcDimensions{registers.srcSurface->width, registers.srcSurface->height, registers.srcSurface->depth};
        size_t srcLayerStride{gpu::texture::GetBlockLinearLayerSize(srcDimensions, 1, 1, 1, registers.srcSurface->blockSize.Height(), registers.srcSurface->blockSize.Depth())};
        size_t srcLayerAddress{*registers.offsetIn + (registers.srcSurface->layer * srcLayerStride)};

        // Get source address
        auto srcMappings{channelCtx.asCtx->gmmu.TranslateRange(*registers.offsetIn, srcLayerStride)};

        gpu::texture::Dimensions dstDimensions{*registers.lineLengthIn, *registers.lineCount, registers.srcSurface->depth};
        size_t dstSize{*registers.pitchOut * dstDimensions.height * dstDimensions.depth}; // If remapping is not enabled there are only 1 bytes per pixel

        // Get destination address
        auto dstMappings{channelCtx.asCtx->gmmu.TranslateRange(*registers.offsetOut, dstSize)};

        auto copyFunc{[&](u8 *src, u8 *dst) {
            if ((util::AlignDown(srcDimensions.width, 64) != util::AlignDown(dstDimensions.width, 64))
                || registers.srcSurface->origin.x || registers.srcSurface->origin.y) {
                gpu::texture::CopyBlockLinearToPitchSubrect(
                    dstDimensions, srcDimensions,
                    1, 1, 1, *registers.pitchOut,
                    registers.srcSurface->blockSize.Height(), registers.srcSurface->blockSize.Depth(),
                    src, dst,
                    registers.srcSurface->origin.x, registers.srcSurface->origin.y
                );
            } else [[likely]] {
                gpu::texture::CopyBlockLinearToPitch(
                    dstDimensions,
                    1, 1, 1, *registers.pitchOut,
                    registers.srcSurface->blockSize.Height(), registers.srcSurface->blockSize.Depth(),
                    src, dst
                );
            }
        }};

        LOGD("{}x{}x{}@0x{:X} -> {}x{}x{}@0x{:X}", srcDimensions.width, srcDimensions.height, srcDimensions.depth, srcLayerAddress, dstDimensions.width, dstDimensions.height, dstDimensions.depth, u64{*registers.offsetOut});

        if (srcMappings.size() != 1 || dstMappings.size() != 1 || HasUnmappedRange(srcMappings) || HasUnmappedRange(dstMappings)) [[unlikely]]
            HandleSplitCopy(srcMappings, dstMappings, srcLayerStride, dstSize, copyFunc);
        else [[likely]]
            copyFunc(srcMappings.front().data(), dstMappings.front().data());
    }

    void MaxwellDma::CopyPitchToBlockLinear() {
        if (registers.dstSurface->blockSize.Width() != 1) [[unlikely]] {
            LOGE("Blocklinear surfaces with a non-one block width are unsupported on the Tegra X1: {}", registers.dstSurface->blockSize.Width());
            return;
        }

        gpu::texture::Dimensions srcDimensions{*registers.lineLengthIn, *registers.lineCount, registers.dstSurface->depth};
        size_t srcLines{static_cast<size_t>(srcDimensions.height) * srcDimensions.depth};
        // The last line only needs `lineLengthIn` bytes and not a whole pitch, asking for the full pitch made the read run past the end of the mapping (page fault)
        size_t srcSize{srcLines ? (*registers.pitchIn * (srcLines - 1)) + *registers.lineLengthIn : 0}; // If remapping is not enabled there are only 1 bytes per pixel

        // Get source address
        auto srcMappings{channelCtx.asCtx->gmmu.TranslateRange(*registers.offsetIn, srcSize)};

        gpu::texture::Dimensions dstDimensions{registers.dstSurface->width, registers.dstSurface->height, registers.dstSurface->depth};
        size_t dstLayerStride{gpu::texture::GetBlockLinearLayerSize(dstDimensions, 1, 1, 1, registers.dstSurface->blockSize.Height(), registers.dstSurface->blockSize.Depth())};
        size_t dstLayerAddress{*registers.offsetOut + (registers.dstSurface->layer * dstLayerStride)};

        // Get destination address
        auto dstMappings{channelCtx.asCtx->gmmu.TranslateRange(*registers.offsetOut, dstLayerStride)};

        LOGD("{}x{}x{}@0x{:X} -> {}x{}x{}@0x{:X}", srcDimensions.width, srcDimensions.height, srcDimensions.depth, u64{*registers.offsetIn}, dstDimensions.width, dstDimensions.height, dstDimensions.depth, dstLayerAddress);

        auto copyFunc{[&](u8 *src, u8 *dst) {
            if ((util::AlignDown(srcDimensions.width, 64) != util::AlignDown(dstDimensions.width, 64))
                || registers.dstSurface->origin.x || registers.dstSurface->origin.y) {
                gpu::texture::CopyPitchToBlockLinearSubrect(
                    srcDimensions, dstDimensions,
                    1, 1, 1, *registers.pitchIn,
                    registers.dstSurface->blockSize.Height(), registers.dstSurface->blockSize.Depth(),
                    src, dst,
                    registers.dstSurface->origin.x, registers.dstSurface->origin.y
                );
            } else [[likely]] {
                gpu::texture::CopyPitchToBlockLinear(
                    srcDimensions,
                    1, 1, 1, *registers.pitchIn,
                    registers.dstSurface->blockSize.Height(), registers.dstSurface->blockSize.Depth(),
                    src, dst
                );
            }
        }};

        if (srcMappings.size() != 1 || dstMappings.size() != 1 || HasUnmappedRange(srcMappings) || HasUnmappedRange(dstMappings)) [[unlikely]]
            HandleSplitCopy(srcMappings, dstMappings, srcSize, dstLayerStride, copyFunc);
        else [[likely]]
            copyFunc(srcMappings.front().data(), dstMappings.front().data());
    }

    void MaxwellDma::ReleaseSemaphore() {
        if (registers.launchDma->reductionEnable) [[unlikely]]
            LOGW("Semaphore reduction is unimplemented!");

        u64 address{registers.semaphore->address};
        u64 payload{registers.semaphore->payload};
        switch (registers.launchDma->semaphoreType) {
            case Registers::LaunchDma::SemaphoreType::ReleaseOneWordSemaphore:
                channelCtx.asCtx->gmmu.Write(address, payload);
                LOGD("address: 0x{:X} payload: {}", address, payload);
                break;
            case Registers::LaunchDma::SemaphoreType::ReleaseFourWordSemaphore: {
                // Write timestamp first to ensure correct ordering
                u64 timestamp{GetGpuTimeTicks()};
                channelCtx.asCtx->gmmu.Write(address + 8, timestamp);
                channelCtx.asCtx->gmmu.Write(address, payload);
                LOGD("address: 0x{:X} payload: {} timestamp: {}", address, payload, timestamp);
                break;
            }
            default:
                break;
        }
    }

    void MaxwellDma::CallMethodBatchNonInc(u32 method, span<u32> arguments) {
        for (u32 argument : arguments)
            HandleMethod(method, argument);
    }
}
