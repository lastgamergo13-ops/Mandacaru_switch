// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <common.h>
#include <common/settings.h>
#include "gl/texture_gallery.h"

namespace skyline::gpu {
    /**
     * @brief Decides which graphics API the GPU is going to use from what the user asked for and writes the decision to the log
     * @note The OpenGL ES backend can't render the guest yet (item E12 of the report): choosing it only shows a test screen (see gl::DrawTestPattern), so 'Automatic' never picks it for now.
     *       When the backend can render games, 'Automatic' picks Vulkan unless the device can't use it (no Vulkan 1.1 or missing required extensions) and OpenGL ES 3.2 is available
     * @note When OpenGL ES is chosen there is NO fallback to Vulkan: if the device can't create an OpenGL ES 3.2 context, or anything of the OpenGL ES backend fails, the emulator fails with the reason in the log
     */
    inline GraphicsApi ResolveGraphicsApi(GraphicsApi requested) {
        switch (requested) {
            case GraphicsApi::Vulkan:
                LOGINF("Graphics API: Vulkan (chosen by the user)");
                return GraphicsApi::Vulkan;

            case GraphicsApi::OpenGL:
                LOGW("Graphics API: OpenGL ES (chosen by the user, experimental): the game isn't rendered yet (it still renders with Vulkan), the screen shows the textures of the game decoded by the OpenGL ES code, and there is no fallback to Vulkan if it fails");
                gl::TextureGallery::Get().Enable(); // The textures are copied from the start of the game
                return GraphicsApi::OpenGL;

            case GraphicsApi::Automatic:
            default:
                LOGINF("Graphics API: Vulkan (automatic, the OpenGL ES backend can't render games yet)");
                return GraphicsApi::Vulkan;
        }
    }
}
