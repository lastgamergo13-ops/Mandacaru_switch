// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <vector>
#include <common/trap_manager.h>
#include "gl_guest_memory.h"

namespace skyline::gpu::gl {
    /**
     * @brief An OpenGL ES buffer object with a copy of a range of guest memory (vertex, index, uniform or storage data)
     * @note Every function **must** be called with the OpenGL ES context current on the calling thread
     */
    class GlBuffer {
      private:
        u32 name{};
        size_t size;

      public:
        /**
         * @brief Creates a buffer object of 'size' bytes (the contents are undefined until Update)
         */
        GlBuffer(size_t size);

        ~GlBuffer();

        GlBuffer(const GlBuffer &) = delete;

        GlBuffer &operator=(const GlBuffer &) = delete;

        /**
         * @brief Replaces the contents of the buffer, 'data' has to have 'Size()' bytes
         */
        void Update(const u8 *data);

        u32 Name() const {
            return name;
        }

        size_t Size() const {
            return size;
        }
    };

    /**
     * @brief The OpenGL ES buffers of the ranges of guest memory that are in use, a buffer is created the first time a range is asked for and uploaded again when the guest CPU wrote to it
     * @note Same as GlTextureCache: guest writes are detected with the traps of the guest mappings and the memory is read through a mirror without traps
     * @note A range is a buffer of its own (two overlapping ranges don't share contents), and the GPU doesn't write to these buffers yet (no transform feedback or compute writes)
     * @note Every function **must** be called with the OpenGL ES context current on the calling thread
     */
    class GlBufferCache {
      private:
        struct Entry {
            span<u8> guest;
            std::unique_ptr<GlBuffer> buffer;
            GuestMirror mirror;
            std::optional<TrapHandle> trap;
            std::atomic<bool> dirty{true};
        };

        const DeviceState &state;
        std::vector<std::shared_ptr<Entry>> entries;

        void Synchronize(Entry &entry);

      public:
        GlBufferCache(const DeviceState &state);

        ~GlBufferCache();

        /**
         * @return The buffer of a range of guest memory with the current contents of the memory
         */
        GlBuffer &Get(span<u8> guestRange);

        void Clear();
    };
}
