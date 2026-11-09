// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <optional>
#include <tsl/robin_map.h>
#include <shader_compiler/shader_info.h>
#include <gpu/texture/texture.h>
#include <gpu/shader_manager.h>
#include <gpu/memory_manager.h>
#include "common.h"
#include "tic.h"

namespace skyline::gpu::interconnect {
    class TexturePoolState : dirty::CachedManualDirty {
      public:
        struct EngineRegisters {
            const engine_common::TexHeaderPool &texHeaderPool;

            void DirtyBind(DirtyManager &manager, dirty::Handle handle) const;
        };

      private:
        dirty::BoundSubresource<EngineRegisters> engine;

      public:
        span<TextureImageControl> textureHeaders;

        TexturePoolState(dirty::Handle dirtyHandle, DirtyManager &manager, const EngineRegisters &engine);

        void Flush(InterconnectContext &ctx);

        void PurgeCaches();
    };

    class Textures {
      private:
        std::shared_ptr<TextureView> nullTextureView{};
        std::optional<memory::Buffer> nullTexelBuffer; //!< Backing of the buffer view that is bound to the texel buffer descriptors that aren't backed by a guest buffer
        std::optional<vk::raii::BufferView> nullTexelBufferView;
        dirty::ManualDirtyState<TexturePoolState> texturePool;

        tsl::robin_map<TextureImageControl, std::shared_ptr<TextureView>, util::ObjectHash<TextureImageControl>> textureHeaderStore;

        struct CacheEntry {
            TextureImageControl tic;
            TextureView *view;
            u64 sequenceNumber;
        };
        std::vector<CacheEntry> textureHeaderCache;

      public:
        Textures(DirtyManager &manager, const TexturePoolState::EngineRegisters &engine);

        void MarkAllDirty();

        TextureView *GetTexture(InterconnectContext &ctx, u32 index, Shader::TextureType shaderType);

        ShaderManager::TextureInfo GetTextureInfo(InterconnectContext &ctx, u32 index);

        /**
         * @return A 1x1 texture that can be bound as a storage image, used for the storage image descriptors that don't have a texture so they never stay undefined
         */
        TextureView *GetNullTextureView(InterconnectContext &ctx);

        /**
         * @return A buffer view that can be bound as an uniform or storage texel buffer, used for the texel buffer descriptors that don't have a guest buffer so they never stay undefined
         */
        vk::BufferView GetNullTexelBufferView(InterconnectContext &ctx);
    };
}
