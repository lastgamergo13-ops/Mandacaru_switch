// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <memory>
#include "backend_interfaces.h"

namespace skyline {
    struct DeviceState;
}

namespace skyline::soc::gm20b {
    struct ChannelContext;
}

namespace skyline::gpu {
    /**
     * @brief Creates the backend of an engine for the graphics API that the user chose: the Vulkan interconnect, or when OpenGL ES is chosen the Vulkan interconnect together with the OpenGL ES backend (see gl::Tee*)
     */
    std::unique_ptr<IFermi2DBackend> MakeFermi2DBackend(const DeviceState &state, soc::gm20b::ChannelContext &channelCtx);

    std::unique_ptr<IMaxwellDmaBackend> MakeMaxwellDmaBackend(const DeviceState &state, soc::gm20b::ChannelContext &channelCtx);

    std::unique_ptr<IInline2MemoryBackend> MakeInline2MemoryBackend(const DeviceState &state, soc::gm20b::ChannelContext &channelCtx);

    /**
     * @return If the user chose OpenGL ES as the graphics API
     */
    bool UsesOpenGl(const DeviceState &state);
}
