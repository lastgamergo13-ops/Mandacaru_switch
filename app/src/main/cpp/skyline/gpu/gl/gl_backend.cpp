// SPDX-License-Identifier: MPL-2.0

#include <chrono>
#include <common.h>
#include "gl_backend.h"

namespace skyline::gpu::gl {
    GlStats &GlStats::Get() {
        static GlStats stats;
        return stats;
    }

    void GlStats::Tick() {
        auto nowMs{std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()};
        auto last{lastReportMs.load(std::memory_order_relaxed)};
        if (nowMs - last < 1000 || !lastReportMs.compare_exchange_strong(last, nowMs))
            return;

        auto take{[](std::atomic<u64> &counter) { return counter.exchange(0); }};
        LOGINF("OpenGL ES backend, last second: draws {} (indexed {}, indirect {}, instanced {}, transform feedback {}), clears {} (color {}, depth {}, stencil {}), blits {} (resolve {}), DMA copies {} ({} bytes), DMA clears {}, "
               "inline uploads {} ({} bytes), compute dispatches {}, queries {}, counter resets {}, constant buffer loads {} ({} words)",
               take(draws), take(indexedDraws), take(indirectDraws), take(instancedDraws), take(transformFeedbackDraws),
               take(clears), take(colorClears), take(depthClears), take(stencilClears),
               take(blits), take(resolveBlits),
               take(dmaCopies), take(dmaCopyBytes), take(dmaClears),
               take(uploads), take(uploadBytes),
               take(dispatches), take(queries), take(counterResets),
               take(constantBufferLoads), take(constantBufferWords));
    }

    void GlMaxwell3D::LoadConstantBuffer(span<u32> data, u32 offset) {
        auto &stats{GlStats::Get()};
        stats.constantBufferLoads++;
        stats.constantBufferWords += data.size();
        stats.Tick();
    }

    void GlMaxwell3D::BindConstantBuffer(ShaderStage stage, u32 index, bool enable) {
        GlStats::Get().Tick();
    }

    void GlMaxwell3D::DisableQuickConstantBufferBind() {}

    void GlMaxwell3D::Clear(ClearSurface &clearSurface) {
        auto &stats{GlStats::Get()};
        stats.clears++;
        if (clearSurface.rEnable || clearSurface.gEnable || clearSurface.bEnable || clearSurface.aEnable)
            stats.colorClears++;
        if (clearSurface.zEnable)
            stats.depthClears++;
        if (clearSurface.stencilEnable)
            stats.stencilClears++;
        stats.Tick();
    }

    void GlMaxwell3D::Draw(DrawTopology topology, bool transformFeedbackEnable, bool indexed, u32 count, u32 first, u32 instanceCount, u32 vertexOffset, u32 firstInstance, span<u32> hostIndices) {
        auto &stats{GlStats::Get()};
        stats.draws++;
        if (indexed)
            stats.indexedDraws++;
        if (instanceCount > 1)
            stats.instancedDraws++;
        if (transformFeedbackEnable)
            stats.transformFeedbackDraws++;
        stats.Tick();
    }

    void GlMaxwell3D::DrawIndirect(DrawTopology topology, bool transformFeedbackEnable, bool indexed, span<u8> indirectBuffer, u32 count, u32 stride) {
        auto &stats{GlStats::Get()};
        stats.draws++;
        stats.indirectDraws++;
        stats.Tick();
    }

    void GlMaxwell3D::Query(IOVA address, CounterType type, std::optional<u64> timestamp) {
        GlStats::Get().queries++;
    }

    void GlMaxwell3D::ResetCounter(ClearReportType type) {
        GlStats::Get().counterResets++;
    }

    bool GlMaxwell3D::QueryPresentAtAddress(IOVA address) {
        return false;
    }

    void GlFermi2D::Blit(const Surface &srcSurface, const Surface &dstSurface, float srcRectX, float srcRectY, u32 dstRectWidth, u32 dstRectHeight, u32 dstRectX, u32 dstRectY, float duDx, float dvDy, SampleModeOrigin sampleOrigin, bool resolve, SampleModeFilter filter) {
        auto &stats{GlStats::Get()};
        stats.blits++;
        if (resolve)
            stats.resolveBlits++;
        stats.Tick();
    }

    void GlMaxwellDma::Copy(span<u8> dstMapping, span<u8> srcMapping) {
        auto &stats{GlStats::Get()};
        stats.dmaCopies++;
        stats.dmaCopyBytes += srcMapping.size();
        stats.Tick();
    }

    void GlMaxwellDma::Clear(span<u8> mapping, u32 value) {
        GlStats::Get().dmaClears++;
    }

    void GlInline2Memory::Upload(IOVA dst, span<u8> src) {
        auto &stats{GlStats::Get()};
        stats.uploads++;
        stats.uploadBytes += src.size();
        stats.Tick();
    }

    void GlKeplerCompute::Dispatch(const QMD &qmd) {
        auto &stats{GlStats::Get()};
        stats.dispatches++;
        stats.Tick();
    }
}
