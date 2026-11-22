// SPDX-License-Identifier: MPL-2.0

#include <chrono>
#include <cstring>
#include <GLES3/gl32.h>
#include <common.h>
#include <soc/gm20b/channel.h>
#include <gpu/interconnect/maxwell_3d/packed_pipeline_state.h>
#include "gl_command_queue.h"
#include "gl_render_target_cache.h"
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
        bool clearsColor{clearSurface.rEnable || clearSurface.gEnable || clearSurface.bEnable || clearSurface.aEnable};
        if (clearsColor)
            stats.colorClears++;
        if (clearSurface.zEnable)
            stats.depthClears++;
        if (clearSurface.stencilEnable)
            stats.stencilClears++;
        stats.Tick();

        if (!clearsColor)
            return; // Depth and stencil clears aren't handled yet (there are no depth render targets in the OpenGL ES backend)

        // The rectangle of the clear is the surface clip, cut by what the clear is told to respect (the same as the Vulkan interconnect does)
        i32 left{registers.surfaceClip.horizontal.x}, top{registers.surfaceClip.vertical.y};
        i32 right{left + registers.surfaceClip.horizontal.width}, bottom{top + registers.surfaceClip.vertical.height};
        auto intersect{[&](i32 otherLeft, i32 otherTop, i32 otherRight, i32 otherBottom) {
            left = std::max(left, otherLeft);
            top = std::max(top, otherTop);
            right = std::min(right, otherRight);
            bottom = std::min(bottom, otherBottom);
        }};

        const auto &control{registers.clearSurfaceControl};
        if (control.useClearRect)
            intersect(registers.clearRect.horizontal.xMin, registers.clearRect.vertical.yMin, registers.clearRect.horizontal.xMax, registers.clearRect.vertical.yMax);
        if (control.useScissor0 && registers.scissor0.enable)
            intersect(registers.scissor0.horizontal.xMin, registers.scissor0.vertical.yMin, registers.scissor0.horizontal.xMax, registers.scissor0.vertical.yMax);
        if (control.useViewportClip0)
            intersect(registers.viewportClip0.horizontal.x0, registers.viewportClip0.vertical.y0, registers.viewportClip0.horizontal.x0 + registers.viewportClip0.horizontal.width, registers.viewportClip0.vertical.y0 + registers.viewportClip0.vertical.height);
        if (right <= left || bottom <= top)
            return;

        // The render target that the clear is for, described the way the Vulkan interconnect does (see ColorRenderTargetState::Flush)
        const auto &target{registers.colorTargets[registers.ctSelect[clearSurface.mrtSelect]]};
        if (target.format == engine::ColorTarget::Format::Disabled)
            return;

        GuestTexture guest{};
        {
            auto packed{std::make_unique<gpu::interconnect::maxwell3d::PackedPipelineState>()};
            packed->SetColorRenderTargetFormat(0, target.format);
            guest.format = packed->GetColorRenderTargetFormat(0);
        }
        guest.aspect = vk::ImageAspectFlagBits::eColor;
        guest.baseArrayLayer = target.layerOffset;

        bool thirdDimensionDefinesArraySize{target.memory.thirdDimensionControl == engine::TargetMemory::ThirdDimensionControl::ThirdDimensionDefinesArraySize};
        guest.layerCount = thirdDimensionDefinesArraySize ? target.thirdDimension : 1;
        guest.viewType = target.thirdDimension > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;

        u32 depth{thirdDimensionDefinesArraySize ? 1U : target.thirdDimension};
        if (target.memory.layout == engine::TargetMemory::Layout::Pitch) {
            guest.dimensions = texture::Dimensions{target.width / guest.format->bpb, target.height, depth};
            guest.tileConfig = texture::TileConfig{.mode = texture::TileMode::Pitch, .pitch = target.width};
        } else {
            guest.dimensions = texture::Dimensions{target.width, target.height, depth};
            guest.tileConfig = texture::TileConfig{.mode = texture::TileMode::Block, .blockHeight = target.memory.BlockHeight(), .blockDepth = target.memory.BlockDepth()};
        }
        guest.layerStride = (guest.baseArrayLayer > 1 || guest.layerCount > 1) ? target.ArrayPitch() : 0;

        auto mappings{channelCtx.asCtx->gmmu.TranslateRange(target.offset, guest.GetSize())};
        guest.mappings.assign(mappings.begin(), mappings.end());
        if (!guest.MappingsValid())
            return;

        if (guest.tileConfig.mode == texture::TileMode::Block) {
            // The dimensions of a render target are aligned to the block linear layout and the surface clip has the unaligned ones, the texture is created with the unaligned ones when the layout is the same
            u32 underlyingLayerSize{guest.CalculateLayerSize()};
            texture::Dimensions underlyingDimensions{guest.dimensions};
            guest.dimensions = texture::Dimensions{static_cast<u32>(registers.surfaceClip.horizontal.width + registers.surfaceClip.horizontal.x), static_cast<u32>(registers.surfaceClip.vertical.height + registers.surfaceClip.vertical.y), guest.dimensions.depth};
            if (guest.CalculateLayerSize() != underlyingLayerSize)
                guest.dimensions = underlyingDimensions;
        }

        std::array<float, 4> color;
        std::memcpy(color.data(), registers.colorClearValue.data(), sizeof(color));

        struct ClearParameters {
            i32 x, y, width, height;
            bool red, green, blue, alpha;
        } parameters{left, top, right - left, bottom - top, clearSurface.rEnable, clearSurface.gEnable, clearSurface.bEnable, clearSurface.aEnable};

        GlCommandQueue::Get().Push([guest = std::move(guest), color, parameters](GlRenderContext &context) {
            auto *texture{context.renderTargets.FindOrCreate(guest)};
            if (!texture)
                return;

            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, context.renderTargets.Framebuffer(*texture));
            glEnable(GL_SCISSOR_TEST);
            glScissor(parameters.x, parameters.y, parameters.width, parameters.height); // The rows of a guest texture are stored from the top, the same as the rows of the framebuffer of the texture, so there is no flip here
            glColorMask(parameters.red, parameters.green, parameters.blue, parameters.alpha);
            glClearBufferfv(GL_COLOR, 0, color.data());
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDisable(GL_SCISSOR_TEST);
        });
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
