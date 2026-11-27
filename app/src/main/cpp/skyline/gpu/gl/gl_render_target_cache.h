// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <memory>
#include <vector>
#include "gl_texture.h"

namespace skyline::gpu::gl {
    /**
     * @brief The OpenGL ES textures of the render targets of the guest, and the framebuffers that render to them: the guest's GPU writes to these textures so their contents live in the OpenGL ES texture
     *        (they are not uploaded from the guest memory like the ones of GlTextureCache)
     * @note The contents aren't copied back to the guest memory yet: a guest CPU read of a render target isn't supported in the OpenGL ES backend
     * @note Every function **must** be called with the OpenGL ES context current on the calling thread
     */
    class GlRenderTargetCache {
      private:
        static constexpr size_t MaxEntries{64}; //!< The oldest render targets are destroyed past this (resolution changes leave unused ones behind)

        struct Entry {
            GuestTexture guest;
            std::unique_ptr<GlTexture> texture;
            u32 framebuffer{}; //!< A framebuffer with the texture as the first color attachment, 0 until it's needed

            Entry(const GuestTexture &guest) : guest{guest} {}
        };

        std::vector<std::unique_ptr<Entry>> entries;
        std::vector<vk::Format> reportedFormats;

        static bool Matches(const GuestTexture &a, const GuestTexture &b);

        static void Destroy(Entry &entry);

      public:
        ~GlRenderTargetCache();

        /**
         * @return The OpenGL ES texture of a render target (created if it doesn't exist), nullptr if the format isn't handled (it's written to the log once)
         */
        GlTexture *FindOrCreate(const GuestTexture &guest);

        /**
         * @return The render target that was created for a guest texture, nullptr if there isn't one
         */
        GlTexture *Find(const GuestTexture &guest);

        /**
         * @return A framebuffer with the texture as the first color attachment
         * @note The draw framebuffer binding may be changed (to the new framebuffer, if it had to be made), the read binding isn't
         * @note Throws skyline::exception if the framebuffer isn't complete
         */
        u32 Framebuffer(GlTexture &texture);

        /**
         * @return The render target that was created for the guest memory that starts at 'address' (the memory of a buffer that is presented), nullptr if there isn't one
         */
        GlTexture *FindByAddress(const u8 *address);
    };
}
