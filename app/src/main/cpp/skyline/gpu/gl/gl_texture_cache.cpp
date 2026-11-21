// SPDX-License-Identifier: MPL-2.0

#include <kernel/types/KProcess.h>
#include "gl_texture_cache.h"

namespace skyline::gpu::gl {
    GlTextureCache::GlTextureCache(const DeviceState &state) : state{state} {}

    GlTextureCache::~GlTextureCache() {
        Clear();
    }

    bool GlTextureCache::Matches(const GuestTexture &a, const GuestTexture &b) {
        return !a.mappings.empty() && !b.mappings.empty() &&
            a.mappings.front().data() == b.mappings.front().data() && a.mappings.size() == b.mappings.size() &&
            a.dimensions == b.dimensions && a.format == b.format &&
            a.tileConfig.mode == b.tileConfig.mode && a.tileConfig.blockHeight == b.tileConfig.blockHeight && a.tileConfig.blockDepth == b.tileConfig.blockDepth && a.tileConfig.pitch == b.tileConfig.pitch &&
            a.viewType == b.viewType && a.mipLevelCount == b.mipLevelCount && a.layerCount == b.layerCount;
    }

    void GlTextureCache::Destroy(Entry &entry) {
        if (entry.trap) {
            state.process->trap.DeleteTrap(*entry.trap);
            entry.trap.reset();
        }
        entry.texture.reset(); // Needs the context
        entry.mirror.Release();
    }

    void GlTextureCache::Synchronize(Entry &entry) {
        if (!entry.dirty.exchange(false))
            return;

        // The traps have to be set before the contents are read so a write that happens in the middle of the upload isn't lost
        state.process->trap.TrapRegions(*entry.trap, true);

        size_t size{entry.texture->GuestSize()};
        if (entry.mirror.data.size() < size)
            throw exception("The guest memory of a texture ({} bytes) is smaller than what the texture needs ({} bytes)", entry.mirror.data.size(), size);

        entry.texture->Upload(entry.mirror.data.data());
    }

    std::shared_ptr<GlTexture> GlTextureCache::FindOrCreate(const GuestTexture &guest) {
        for (auto &entry : entries) {
            if (Matches(entry->guest, guest)) {
                Synchronize(*entry);
                return entry->texture;
            }
        }

        auto entry{std::make_shared<Entry>(guest)};
        entry->texture = std::make_shared<GlTexture>(guest);
        entry->mirror = CreateGuestMirror(state, guest.mappings);

        std::weak_ptr<Entry> weakEntry{entry};
        entry->trap = state.process->trap.CreateTrap(entry->guest.mappings, [] {}, [] { return true; }, [weakEntry] {
            if (auto locked{weakEntry.lock()})
                locked->dirty = true;
            return true;
        });

        entries.push_back(entry);
        Synchronize(*entry);
        return entry->texture;
    }

    void GlTextureCache::Clear() {
        for (auto &entry : entries)
            Destroy(*entry);
        entries.clear();
    }
}
