// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <common.h>
#include <common/settings.h>

namespace skyline::gpu {
    /**
     * @brief Decides which graphics API the GPU is going to use from what the user asked for and writes the decision to the log
     * @note The OpenGL ES backend can't render the guest yet (item E12 of the report): choosing it only shows a test screen (see gl::DrawTestPattern), so 'Automatic' never picks it for now.
     *       When the backend can render games, 'Automatic' picks Vulkan unless the device can't use it (no Vulkan 1.1 or missing required extensions) and OpenGL ES 3.2 is available
     * @note If the device has no OpenGL ES 3.2 context the presentation engine falls back to Vulkan when it receives the window (it logs why)
     */
    inline GraphicsApi ResolveGraphicsApi(GraphicsApi requested) {
        switch (requested) {
            case GraphicsApi::Vulkan:
                LOGINF("Graphics API: Vulkan (chosen by the user)");
                return GraphicsApi::Vulkan;

            case GraphicsApi::OpenGL:
                LOGW("Graphics API: OpenGL ES (chosen by the user, experimental): only a test screen is shown, the game isn't rendered yet");
                return GraphicsApi::OpenGL;

            case GraphicsApi::Automatic:
            default:
                LOGINF("Graphics API: Vulkan (automatic, the OpenGL ES backend can't render games yet)");
                return GraphicsApi::Vulkan;
        }
    }
}
