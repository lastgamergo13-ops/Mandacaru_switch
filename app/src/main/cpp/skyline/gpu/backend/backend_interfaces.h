// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <optional>
#include <common.h>
#include <soc/gm20b/gmmu.h>
#include <soc/gm20b/engines/maxwell/types.h>
#include <soc/gm20b/engines/fermi/types.h>
#include <soc/gm20b/engines/kepler_compute/qmd.h>

namespace skyline::gpu {
    /**
     * @brief What the Maxwell 3D engine of the guest asks a graphics API backend to do, the Vulkan interconnect and the OpenGL ES backend implement it (item E12 of the report)
     * @note The engine reads the registers itself and passes what changes the picture, everything else is read by the backend from the registers it was given when it was created
     */
    class IMaxwell3DBackend {
      public:
        using IOVA = soc::gm20b::IOVA;
        using ShaderStage = soc::gm20b::engine::maxwell3d::type::ShaderStage;
        using ClearSurface = soc::gm20b::engine::maxwell3d::type::ClearSurface;
        using DrawTopology = soc::gm20b::engine::maxwell3d::type::DrawTopology;
        using CounterType = soc::gm20b::engine::maxwell3d::type::SemaphoreInfo::CounterType;
        using ClearReportType = soc::gm20b::engine::maxwell3d::type::ClearReportValue::Type;

        virtual ~IMaxwell3DBackend() = default;

        /**
         * @brief Loads the given data into the constant buffer pointed by the constant buffer selector starting at the given offset
         */
        virtual void LoadConstantBuffer(span<u32> data, u32 offset) = 0;

        virtual void BindConstantBuffer(ShaderStage stage, u32 index, bool enable) = 0;

        virtual void DisableQuickConstantBufferBind() = 0;

        virtual void Clear(ClearSurface &clearSurface) = 0;

        virtual void Draw(DrawTopology topology, bool transformFeedbackEnable, bool indexed, u32 count, u32 first, u32 instanceCount, u32 vertexOffset, u32 firstInstance, span<u32> hostIndices = {}) = 0;

        virtual void DrawIndirect(DrawTopology topology, bool transformFeedbackEnable, bool indexed, span<u8> indirectBuffer, u32 count, u32 stride) = 0;

        virtual void Query(IOVA address, CounterType type, std::optional<u64> timestamp) = 0;

        virtual void ResetCounter(ClearReportType type) = 0;

        /**
         * @return If a query was written to the given address and its result isn't available yet
         */
        virtual bool QueryPresentAtAddress(IOVA address) = 0;
    };

    class IFermi2DBackend {
      public:
        using Surface = soc::gm20b::engine::fermi2d::type::Surface;
        using SampleModeOrigin = soc::gm20b::engine::fermi2d::type::SampleModeOrigin;
        using SampleModeFilter = soc::gm20b::engine::fermi2d::type::SampleModeFilter;

        virtual ~IFermi2DBackend() = default;

        virtual void Blit(const Surface &srcSurface, const Surface &dstSurface, float srcRectX, float srcRectY, u32 dstRectWidth, u32 dstRectHeight, u32 dstRectX, u32 dstRectY, float duDx, float dvDy, SampleModeOrigin sampleOrigin, bool resolve, SampleModeFilter filter) = 0;
    };

    class IMaxwellDmaBackend {
      public:
        virtual ~IMaxwellDmaBackend() = default;

        virtual void Copy(span<u8> dstMapping, span<u8> srcMapping) = 0;

        virtual void Clear(span<u8> mapping, u32 value) = 0;
    };

    class IInline2MemoryBackend {
      public:
        using IOVA = soc::gm20b::IOVA;

        virtual ~IInline2MemoryBackend() = default;

        virtual void Upload(IOVA dst, span<u8> src) = 0;
    };

    class IKeplerComputeBackend {
      public:
        using QMD = soc::gm20b::engine::kepler_compute::QMD;

        virtual ~IKeplerComputeBackend() = default;

        virtual void Dispatch(const QMD &qmd) = 0;
    };
}
