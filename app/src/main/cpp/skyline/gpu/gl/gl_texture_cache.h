// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <vector>
#include <common/trap_manager.h>
#include "gl_guest_memory.h"
#include "gl_texture.h"

namespace skyline::gpu::gl {
    /**
     * @brief The OpenGL ES textures of the guest textures that are in use: a texture is created the first time it's asked for and uploaded again when the guest CPU wrote to its memory
     * @note The guest writes are detected with the same traps that the Vulkan textures use (write protection of the guest mappings), the data is read from a mirror of the memory that has no traps
     * @note Textures that the GPU writes (render targets) aren't handled here yet: their contents live in the OpenGL ES texture and they need to be copied back to the guest memory when the CPU reads it
     * @note Every function **must** be called with the OpenGL ES context current on the calling thread
     */
    class GlTextureCache {
      private:
        struct Entry {
            GuestTexture guest;
            std::shared_ptr<GlTexture> texture;
            GuestMirror mirror;
            std::optional<TrapHandle> trap;
            std::atomic<bool> dirty{true}; //!< If the guest CPU wrote to the memory of the texture since it was uploaded

            Entry(const GuestTexture &guest) : guest{guest} {}
        };

        const DeviceState &state;
        std::vector<std::shared_ptr<Entry>> entries;

        static bool Matches(const GuestTexture &a, const GuestTexture &b);

        void Synchronize(Entry &entry);

        void Destroy(Entry &entry);

      public:
        GlTextureCache(const DeviceState &state);

        ~GlTextureCache();

        /**
         * @return The OpenGL ES texture of a guest texture with the guest memory contents uploaded, it's created if it doesn't exist
         * @note Throws skyline::exception if the texture isn't handled (see GlTexture)
         */
        std::shared_ptr<GlTexture> FindOrCreate(const GuestTexture &guest);

        /**
         * @brief Destroys every texture
         */
        void Clear();
    };
}
