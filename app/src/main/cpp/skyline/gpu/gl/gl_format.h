// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <vulkan/vulkan.hpp>
#include <common.h>

namespace skyline::gpu::gl {
    /**
     * @brief What has to be done to the guest data (after it left the guest layout) so OpenGL ES can use it
     */
    enum class Conversion : u8 {
        None, //!< The data is used as it is (this includes ASTC, which the driver decodes)
        SwapRedBlue, //!< The guest has BGRA and OpenGL ES RGBA (4 bytes per pixel)
        Bc1, //!< BC1 with alpha to RGBA8
        Bc2, //!< BC2 to RGBA8
        Bc3, //!< BC3 to RGBA8
        Bc4, //!< BC4 to R8
        Bc4Signed, //!< Signed BC4 to R8_SNORM
        Bc5, //!< BC5 to RG8
        Bc5Signed, //!< Signed BC5 to RG8_SNORM
        Bc6, //!< BC6H to RGBA16F
        Bc6Signed, //!< Signed BC6H to RGBA16F
        Bc7, //!< BC7 to RGBA8
    };

    /**
     * @brief How a sampler reads the texture so it looks like what the guest expects
     */
    enum class Swizzle : u8 {
        None,
        Gray, //!< One channel: (R, R, R, 1)
        RedGreen, //!< Two channels: (R, G, 0, 1)
    };

    /**
     * @brief The OpenGL ES side of a guest texture format
     */
    struct GlFormat {
        u32 internalFormat; //!< The sized (or compressed) internal format of the texture storage
        u32 format; //!< The format of the data that is uploaded, unused for compressed internal formats
        u32 type; //!< The type of the data that is uploaded, unused for compressed internal formats
        bool compressed; //!< If the driver gets compressed blocks (ASTC)
        bool isInteger; //!< If it's an integer format, it can't be read through a floating point sampler
        bool isSrgb;
        Conversion conversion;
        Swizzle swizzle;
        u32 hostBytesPerPixel; //!< The size of a pixel of the uploaded data in bytes, 0 for compressed internal formats
    };

    /**
     * @return The OpenGL ES format that a guest format maps to, nullptr if the OpenGL ES backend doesn't handle it yet (depth/stencil formats and some packed ones are in that case)
     */
    const GlFormat *GetGlFormat(vk::Format format);
}
