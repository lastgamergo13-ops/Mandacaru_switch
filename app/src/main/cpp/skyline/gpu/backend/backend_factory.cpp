// SPDX-License-Identifier: MPL-2.0

#include <soc.h>
#include <common/settings.h>
#include <soc/gm20b/channel.h>
#include <gpu/interconnect/fermi_2d.h>
#include <gpu/interconnect/maxwell_dma.h>
#include <gpu/interconnect/inline2memory.h>
#include <gpu/gl/gl_backend.h>
#include "backend_factory.h"

namespace skyline::gpu {
    bool UsesOpenGl(const DeviceState &state) {
        return *state.settings->graphicsApi == GraphicsApi::OpenGL;
    }

    std::unique_ptr<IFermi2DBackend> MakeFermi2DBackend(const DeviceState &state, soc::gm20b::ChannelContext &channelCtx) {
        auto vulkan{std::make_unique<interconnect::Fermi2D>(*state.gpu, channelCtx)};
        if (!UsesOpenGl(state))
            return vulkan;
        return std::make_unique<gl::TeeFermi2D>(std::move(vulkan), std::make_unique<gl::GlFermi2D>());
    }

    std::unique_ptr<IMaxwellDmaBackend> MakeMaxwellDmaBackend(const DeviceState &state, soc::gm20b::ChannelContext &channelCtx) {
        auto vulkan{std::make_unique<interconnect::MaxwellDma>(*state.gpu, channelCtx)};
        if (!UsesOpenGl(state))
            return vulkan;
        return std::make_unique<gl::TeeMaxwellDma>(std::move(vulkan), std::make_unique<gl::GlMaxwellDma>());
    }

    std::unique_ptr<IInline2MemoryBackend> MakeInline2MemoryBackend(const DeviceState &state, soc::gm20b::ChannelContext &channelCtx) {
        auto vulkan{std::make_unique<interconnect::Inline2Memory>(*state.gpu, channelCtx)};
        if (!UsesOpenGl(state))
            return vulkan;
        return std::make_unique<gl::TeeInline2Memory>(std::move(vulkan), std::make_unique<gl::GlInline2Memory>());
    }
}
