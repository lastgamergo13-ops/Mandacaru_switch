// SPDX-License-Identifier: MPL-2.0

#include <GLES3/gl32.h>
#include <kernel/types/KProcess.h>
#include "gl_buffer_cache.h"

namespace skyline::gpu::gl {
    GlBuffer::GlBuffer(size_t size) : size{size} {
        glGenBuffers(1, &name);
        glBindBuffer(GL_COPY_WRITE_BUFFER, name);
        glBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(size), nullptr, GL_DYNAMIC_DRAW);

        if (auto error{glGetError()}; error != GL_NO_ERROR) {
            glDeleteBuffers(1, &name);
            throw exception("Creating an OpenGL ES buffer of {} bytes failed: error 0x{:X}", size, error);
        }
    }

    GlBuffer::~GlBuffer() {
        if (name)
            glDeleteBuffers(1, &name);
    }

    void GlBuffer::Update(const u8 *data) {
        glBindBuffer(GL_COPY_WRITE_BUFFER, name);
        glBufferSubData(GL_COPY_WRITE_BUFFER, 0, static_cast<GLsizeiptr>(size), data);

        if (auto error{glGetError()}; error != GL_NO_ERROR)
            throw exception("Updating an OpenGL ES buffer of {} bytes failed: error 0x{:X}", size, error);
    }

    GlBufferCache::GlBufferCache(const DeviceState &state) : state{state} {}

    GlBufferCache::~GlBufferCache() {
        Clear();
    }

    void GlBufferCache::Synchronize(Entry &entry) {
        if (!entry.dirty.exchange(false))
            return;

        // The traps have to be set before the contents are read so a write that happens in the middle of the upload isn't lost
        state.process->trap.TrapRegions(*entry.trap, true);
        entry.buffer->Update(entry.mirror.data.data());
    }

    GlBuffer &GlBufferCache::Get(span<u8> guestRange) {
        for (auto &entry : entries) {
            if (entry->guest.data() == guestRange.data() && entry->guest.size() == guestRange.size()) {
                Synchronize(*entry);
                return *entry->buffer;
            }
        }

        auto entry{std::make_shared<Entry>()};
        entry->guest = guestRange;
        entry->buffer = std::make_unique<GlBuffer>(guestRange.size());
        boost::container::small_vector<span<u8>, 3> mappings;
        mappings.push_back(guestRange);
        entry->mirror = CreateGuestMirror(state, mappings);

        std::weak_ptr<Entry> weakEntry{entry};
        span<span<u8>> regions{&entry->guest, 1};
        entry->trap = state.process->trap.CreateTrap(regions, [] {}, [] { return true; }, [weakEntry] {
            if (auto locked{weakEntry.lock()})
                locked->dirty = true;
            return true;
        });

        entries.push_back(entry);
        Synchronize(*entry);
        return *entry->buffer;
    }

    void GlBufferCache::Clear() {
        for (auto &entry : entries) {
            if (entry->trap)
                state.process->trap.DeleteTrap(*entry->trap);
            entry->buffer.reset(); // Needs the context
            entry->mirror.Release();
        }
        entries.clear();
    }
}
