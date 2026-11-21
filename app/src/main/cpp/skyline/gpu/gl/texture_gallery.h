// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <gpu/texture/texture.h>
#include "gl_texture.h"

namespace skyline::gpu::gl {
    /**
     * @brief A copy of the guest memory of a texture the game created, it is uploaded by the OpenGL ES thread
     */
    struct GalleryEntry {
        u32 number; //!< The order in which the gallery received the texture
        GuestTexture guest; //!< The description of the texture (dimensions, format, tiling), its mappings aren't used: 'data' has the bytes
        std::vector<u8> data; //!< The guest bytes of the whole texture (every layer and mip level) in the guest layout
    };

    /**
     * @brief The textures that the game creates, copied when they are created so the OpenGL ES thread can upload them on its own with the code the OpenGL ES backend uses (GlTexture: guest layout,
     *        block compression, mip levels, layers) and show them on the screen
     * @note This is a tool to check the OpenGL ES texture path (item E12, phase 2) without a renderer: the game still renders with Vulkan, the OpenGL ES thread only shows the textures it created
     */
    class TextureGallery {
      private:
        static constexpr size_t MaxPending{8}; //!< The oldest textures that weren't uploaded are dropped past this (they are copies of guest memory, so it's kept small)

        std::atomic<bool> enabled{};
        std::mutex mutex;
        std::deque<GalleryEntry> pending;
        std::vector<vk::Format> reportedFormats; //!< The formats that the backend doesn't handle that were already written to the log
        u32 counter{};

      public:
        static TextureGallery &Get();

        /**
         * @brief Starts to copy the textures, it's called when the OpenGL ES API is chosen
         */
        void Enable();

        bool IsEnabled() const {
            return enabled.load(std::memory_order_relaxed);
        }

        /**
         * @brief Copies the guest data of a texture that was just created, small textures, the ones that are too big and the types that aren't handled are ignored
         * @note Called from the thread that creates textures
         */
        void Submit(const GuestTexture &guest);

        /**
         * @brief Takes the oldest texture that wasn't uploaded yet
         * @return If there was one
         */
        bool TakePending(GalleryEntry &entry);
    };

    /**
     * @brief Uploads the textures of the gallery to OpenGL ES textures and draws them in a grid, every texture goes through its mip levels (one level for a couple of seconds each)
     * @note Every function **must** be called with the OpenGL ES context current on the calling thread
     */
    class GalleryView {
      private:
        static constexpr size_t Columns{4};
        static constexpr size_t Rows{3};
        static constexpr size_t SlotCount{Columns * Rows};
        static constexpr u64 FramesPerLevel{120}; //!< How many frames a mip level is shown before the next one

        struct Slot {
            std::unique_ptr<GlTexture> texture;
            u32 shownLevel{~0U}; //!< The mip level that is set to be sampled
        };

        u32 program{};
        i32 rectLocation{};
        i32 textureLocation{};
        i32 srgbLocation{};
        std::array<Slot, SlotCount> slots{};
        size_t nextSlot{};

        void Show(const GalleryEntry &entry);

      public:
        /**
         * @brief Compiles the program that draws the textures
         * @note Throws skyline::exception if that fails, the log has the compiler's message
         */
        GalleryView();

        ~GalleryView();

        GalleryView(const GalleryView &) = delete;

        GalleryView &operator=(const GalleryView &) = delete;

        /**
         * @brief Uploads the pending textures (a few per call, so a frame isn't held for long) and draws the grid with a white bar at the bottom that moves with every frame
         */
        void Draw(i32 width, i32 height, u64 frame);
    };
}
