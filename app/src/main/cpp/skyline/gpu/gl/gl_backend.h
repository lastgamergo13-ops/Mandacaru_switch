// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <atomic>
#include <memory>
#include <gpu/backend/backend_interfaces.h>

namespace skyline::gpu::gl {
    /**
     * @brief What the guest asked the OpenGL ES backend to do, counted since the last report: it is written to the log once per second so what the game does (draws, clears, blits, copies) is known
     * @note This is the first stage of the OpenGL ES backend (item E12, phase 3): it only counts. The calls are also executed by the Vulkan interconnect (see the Tee classes) so the game keeps running
     */
    class GlStats {
      private:
        std::atomic<i64> lastReportMs{};

      public:
        std::atomic<u64> draws{}, indexedDraws{}, indirectDraws{}, transformFeedbackDraws{}, instancedDraws{};
        std::atomic<u64> clears{}, colorClears{}, depthClears{}, stencilClears{};
        std::atomic<u64> blits{}, resolveBlits{};
        std::atomic<u64> dmaCopies{}, dmaCopyBytes{}, dmaClears{};
        std::atomic<u64> uploads{}, uploadBytes{};
        std::atomic<u64> dispatches{};
        std::atomic<u64> queries{}, counterResets{};
        std::atomic<u64> constantBufferLoads{}, constantBufferWords{};

        static GlStats &Get();

        /**
         * @brief Writes the counters to the log if a second has passed since the last report and starts counting again
         */
        void Tick();
    };

    class GlMaxwell3D final : public IMaxwell3DBackend {
      public:
        void LoadConstantBuffer(span<u32> data, u32 offset) override;

        void BindConstantBuffer(ShaderStage stage, u32 index, bool enable) override;

        void DisableQuickConstantBufferBind() override;

        void Clear(ClearSurface &clearSurface) override;

        void Draw(DrawTopology topology, bool transformFeedbackEnable, bool indexed, u32 count, u32 first, u32 instanceCount, u32 vertexOffset, u32 firstInstance, span<u32> hostIndices = {}) override;

        void DrawIndirect(DrawTopology topology, bool transformFeedbackEnable, bool indexed, span<u8> indirectBuffer, u32 count, u32 stride) override;

        void Query(IOVA address, CounterType type, std::optional<u64> timestamp) override;

        void ResetCounter(ClearReportType type) override;

        bool QueryPresentAtAddress(IOVA address) override;
    };

    class GlFermi2D final : public IFermi2DBackend {
      public:
        void Blit(const Surface &srcSurface, const Surface &dstSurface, float srcRectX, float srcRectY, u32 dstRectWidth, u32 dstRectHeight, u32 dstRectX, u32 dstRectY, float duDx, float dvDy, SampleModeOrigin sampleOrigin, bool resolve, SampleModeFilter filter) override;
    };

    class GlMaxwellDma final : public IMaxwellDmaBackend {
      public:
        void Copy(span<u8> dstMapping, span<u8> srcMapping) override;

        void Clear(span<u8> mapping, u32 value) override;
    };

    class GlInline2Memory final : public IInline2MemoryBackend {
      public:
        void Upload(IOVA dst, span<u8> src) override;
    };

    class GlKeplerCompute final : public IKeplerComputeBackend {
      public:
        void Dispatch(const QMD &qmd) override;
    };

    /**
     * @brief Calls both a primary backend (the Vulkan interconnect, which does the work and answers) and the OpenGL ES backend (which, for now, only counts what it is asked to do)
     * @note A failure of the OpenGL ES backend isn't handled on purpose: it ends the emulator so it's clear what the OpenGL ES backend is missing
     */
    class TeeMaxwell3D final : public IMaxwell3DBackend {
      private:
        std::unique_ptr<IMaxwell3DBackend> primary, shadow;

      public:
        TeeMaxwell3D(std::unique_ptr<IMaxwell3DBackend> primary, std::unique_ptr<IMaxwell3DBackend> shadow) : primary{std::move(primary)}, shadow{std::move(shadow)} {}

        void LoadConstantBuffer(span<u32> data, u32 offset) override {
            primary->LoadConstantBuffer(data, offset);
            shadow->LoadConstantBuffer(data, offset);
        }

        void BindConstantBuffer(ShaderStage stage, u32 index, bool enable) override {
            primary->BindConstantBuffer(stage, index, enable);
            shadow->BindConstantBuffer(stage, index, enable);
        }

        void DisableQuickConstantBufferBind() override {
            primary->DisableQuickConstantBufferBind();
            shadow->DisableQuickConstantBufferBind();
        }

        void Clear(ClearSurface &clearSurface) override {
            primary->Clear(clearSurface);
            shadow->Clear(clearSurface);
        }

        void Draw(DrawTopology topology, bool transformFeedbackEnable, bool indexed, u32 count, u32 first, u32 instanceCount, u32 vertexOffset, u32 firstInstance, span<u32> hostIndices = {}) override {
            primary->Draw(topology, transformFeedbackEnable, indexed, count, first, instanceCount, vertexOffset, firstInstance, hostIndices);
            shadow->Draw(topology, transformFeedbackEnable, indexed, count, first, instanceCount, vertexOffset, firstInstance, hostIndices);
        }

        void DrawIndirect(DrawTopology topology, bool transformFeedbackEnable, bool indexed, span<u8> indirectBuffer, u32 count, u32 stride) override {
            primary->DrawIndirect(topology, transformFeedbackEnable, indexed, indirectBuffer, count, stride);
            shadow->DrawIndirect(topology, transformFeedbackEnable, indexed, indirectBuffer, count, stride);
        }

        void Query(IOVA address, CounterType type, std::optional<u64> timestamp) override {
            primary->Query(address, type, timestamp);
            shadow->Query(address, type, timestamp);
        }

        void ResetCounter(ClearReportType type) override {
            primary->ResetCounter(type);
            shadow->ResetCounter(type);
        }

        bool QueryPresentAtAddress(IOVA address) override {
            shadow->QueryPresentAtAddress(address);
            return primary->QueryPresentAtAddress(address);
        }
    };

    class TeeFermi2D final : public IFermi2DBackend {
      private:
        std::unique_ptr<IFermi2DBackend> primary, shadow;

      public:
        TeeFermi2D(std::unique_ptr<IFermi2DBackend> primary, std::unique_ptr<IFermi2DBackend> shadow) : primary{std::move(primary)}, shadow{std::move(shadow)} {}

        void Blit(const Surface &srcSurface, const Surface &dstSurface, float srcRectX, float srcRectY, u32 dstRectWidth, u32 dstRectHeight, u32 dstRectX, u32 dstRectY, float duDx, float dvDy, SampleModeOrigin sampleOrigin, bool resolve, SampleModeFilter filter) override {
            primary->Blit(srcSurface, dstSurface, srcRectX, srcRectY, dstRectWidth, dstRectHeight, dstRectX, dstRectY, duDx, dvDy, sampleOrigin, resolve, filter);
            shadow->Blit(srcSurface, dstSurface, srcRectX, srcRectY, dstRectWidth, dstRectHeight, dstRectX, dstRectY, duDx, dvDy, sampleOrigin, resolve, filter);
        }
    };

    class TeeMaxwellDma final : public IMaxwellDmaBackend {
      private:
        std::unique_ptr<IMaxwellDmaBackend> primary, shadow;

      public:
        TeeMaxwellDma(std::unique_ptr<IMaxwellDmaBackend> primary, std::unique_ptr<IMaxwellDmaBackend> shadow) : primary{std::move(primary)}, shadow{std::move(shadow)} {}

        void Copy(span<u8> dstMapping, span<u8> srcMapping) override {
            primary->Copy(dstMapping, srcMapping);
            shadow->Copy(dstMapping, srcMapping);
        }

        void Clear(span<u8> mapping, u32 value) override {
            primary->Clear(mapping, value);
            shadow->Clear(mapping, value);
        }
    };

    class TeeInline2Memory final : public IInline2MemoryBackend {
      private:
        std::unique_ptr<IInline2MemoryBackend> primary, shadow;

      public:
        TeeInline2Memory(std::unique_ptr<IInline2MemoryBackend> primary, std::unique_ptr<IInline2MemoryBackend> shadow) : primary{std::move(primary)}, shadow{std::move(shadow)} {}

        void Upload(IOVA dst, span<u8> src) override {
            primary->Upload(dst, src);
            shadow->Upload(dst, src);
        }
    };

    class TeeKeplerCompute final : public IKeplerComputeBackend {
      private:
        std::unique_ptr<IKeplerComputeBackend> primary, shadow;

      public:
        TeeKeplerCompute(std::unique_ptr<IKeplerComputeBackend> primary, std::unique_ptr<IKeplerComputeBackend> shadow) : primary{std::move(primary)}, shadow{std::move(shadow)} {}

        void Dispatch(const QMD &qmd) override {
            primary->Dispatch(qmd);
            shadow->Dispatch(qmd);
        }
    };
}
