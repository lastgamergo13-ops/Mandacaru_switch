// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <vector>
#include <gpu/texture/texture.h>
#include "gl_format.h"

namespace skyline::gpu::gl {
    /**
     * @brief A guest texture as an OpenGL ES texture: the storage with all the mip levels and layers, and the code that takes the guest layout (block linear, pitch, linear) and the guest format (BC)
     *        to what the OpenGL ES driver takes
     * @note Every function **must** be called with the OpenGL ES context current on the calling thread
     */
    class GlTexture {
      private:
        GuestTexture guest;
        const GlFormat *glFormat;
        std::vector<texture::MipLevelLayout> mipLayouts; //!< The layout of every mip level of a layer (the sizes are the ones of the guest format)
        u32 name{};
        u32 target{}; //!< GL_TEXTURE_2D, GL_TEXTURE_2D_ARRAY, GL_TEXTURE_CUBE_MAP or GL_TEXTURE_3D
        u32 levels{};
        u32 layers{}; //!< The number of layers (6 for a cube map, 1 for 2D and 3D textures)

        void UploadLevel(u32 layer, u32 level, const texture::MipLevelLayout &layout, const std::vector<u8> &linear);

      public:
        /**
         * @brief Creates the texture and its storage, the contents are uploaded with Upload
         * @note Throws skyline::exception if the format or the type of the texture isn't handled
         */
        GlTexture(const GuestTexture &guest);

        ~GlTexture();

        GlTexture(const GlTexture &) = delete;

        GlTexture &operator=(const GlTexture &) = delete;

        /**
         * @brief Uploads every layer and mip level from the guest memory of the texture
         * @param guestData The guest bytes of the texture (the start of the first layer), they have to be readable for 'GuestTexture::GetLayerStride() * layers' bytes
         */
        void Upload(const u8 *guestData);

        u32 Name() const {
            return name;
        }

        u32 Target() const {
            return target;
        }

        u32 Levels() const {
            return levels;
        }

        u32 Layers() const {
            return layers;
        }

        u32 Width() const {
            return guest.dimensions.width;
        }

        u32 Height() const {
            return guest.dimensions.height;
        }

        const GlFormat &Format() const {
            return *glFormat;
        }

        const GuestTexture &Guest() const {
            return guest;
        }

        /**
         * @return How many bytes of guest memory Upload reads
         */
        size_t GuestSize();
    };
}
