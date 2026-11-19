// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <common.h>
#include <common/settings.h>

namespace skyline::gpu {
    /**
     * @brief Decides which graphics API the GPU is going to use from what the user asked for and writes the decision to the log
     * @note The OpenGL ES backend doesn't exist yet (item E12 of the report), so for now every choice ends in Vulkan. When it exists: 'Vulkan' and 'OpenGL' are always honored if the device can do it, and
     *       'Automatic' picks Vulkan unless the device can't use it (no Vulkan 1.1 or missing required extensions) and OpenGL ES 3.2 is available
     */
    inline GraphicsApi ResolveGraphicsApi(GraphicsApi requested) {
        constexpr bool OpenGlBackendAvailable{false}; // Becomes true when the OpenGL ES backend can render the guest

        switch (requested) {
            case GraphicsApi::Vulkan:
                LOGINF("Graphics API: Vulkan (chosen by the user)");
                return GraphicsApi::Vulkan;

            case GraphicsApi::OpenGL:
                if (!OpenGlBackendAvailable) {
                    LOGW("Graphics API: OpenGL ES was chosen but its backend isn't implemented yet, using Vulkan instead");
                    return GraphicsApi::Vulkan;
                }
                LOGINF("Graphics API: OpenGL ES (chosen by the user)");
                return GraphicsApi::OpenGL;

            case GraphicsApi::Automatic:
            default:
                LOGINF("Graphics API: Vulkan (automatic{})", OpenGlBackendAvailable ? "" : ", the OpenGL ES backend isn't available yet");
                return GraphicsApi::Vulkan;
        }
    }
}
