// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <GLES3/gl32.h>
#include <common.h>
#include "gl_render_target_cache.h"

namespace skyline::gpu::gl {
    bool GlRenderTargetCache::Matches(const GuestTexture &a, const GuestTexture &b) {
        return !a.mappings.empty() && !b.mappings.empty() &&
            a.mappings.front().data() == b.mappings.front().data() && a.mappings.size() == b.mappings.size() &&
            a.dimensions == b.dimensions && a.format == b.format &&
            a.tileConfig.mode == b.tileConfig.mode && a.tileConfig.blockHeight == b.tileConfig.blockHeight && a.tileConfig.blockDepth == b.tileConfig.blockDepth && a.tileConfig.pitch == b.tileConfig.pitch &&
            a.viewType == b.viewType && a.layerCount == b.layerCount;
    }

    void GlRenderTargetCache::Destroy(Entry &entry) {
        if (entry.framebuffer)
            glDeleteFramebuffers(1, &entry.framebuffer);
        entry.framebuffer = 0;
        entry.texture.reset();
    }

    GlRenderTargetCache::~GlRenderTargetCache() {
        for (auto &entry : entries)
            Destroy(*entry);
    }

    GlTexture *GlRenderTargetCache::FindOrCreate(const GuestTexture &guest) {
        for (auto &entry : entries)
            if (Matches(entry->guest, guest))
                return entry->texture.get();

        if (!GetGlFormat(guest.format->vkFormat)) {
            if (std::ranges::find(reportedFormats, guest.format->vkFormat) == reportedFormats.end()) {
                reportedFormats.push_back(guest.format->vkFormat);
                LOGINF("OpenGL ES backend: the render target format {} isn't handled yet", vk::to_string(guest.format->vkFormat));
            }
            return nullptr;
        }

        if (entries.size() >= MaxEntries) {
            Destroy(*entries.front());
            entries.erase(entries.begin());
        }

        auto entry{std::make_unique<Entry>(guest)};
        entry->texture = std::make_unique<GlTexture>(guest);
        LOGINF("OpenGL ES backend: render target {}x{} {} created", guest.dimensions.width, guest.dimensions.height, vk::to_string(guest.format->vkFormat));
        entries.push_back(std::move(entry));
        return entries.back()->texture.get();
    }

    u32 GlRenderTargetCache::Framebuffer(GlTexture &texture) {
        for (auto &entry : entries) {
            if (entry->texture.get() != &texture)
                continue;

            if (!entry->framebuffer) {
                glGenFramebuffers(1, &entry->framebuffer);
                glBindFramebuffer(GL_FRAMEBUFFER, entry->framebuffer);
                if (texture.Target() == GL_TEXTURE_2D)
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.Name(), 0);
                else
                    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture.Name(), 0, 0);

                auto status{glCheckFramebufferStatus(GL_FRAMEBUFFER)};
                if (status != GL_FRAMEBUFFER_COMPLETE)
                    throw exception("The OpenGL ES framebuffer of a render target ({}x{} {}) isn't complete: 0x{:X}", texture.Width(), texture.Height(), vk::to_string(texture.Guest().format->vkFormat), status);
            }
            return entry->framebuffer;
        }

        throw exception("The OpenGL ES texture isn't a render target");
    }

    GlTexture *GlRenderTargetCache::FindByAddress(const u8 *address) {
        for (auto it{entries.rbegin()}; it != entries.rend(); ++it) // The newest first
            if (!(*it)->guest.mappings.empty() && (*it)->guest.mappings.front().data() == address)
                return (*it)->texture.get();
        return nullptr;
    }
}
