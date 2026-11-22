// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <functional>
#include <mutex>
#include <vector>
#include <common.h>

namespace skyline::gpu::gl {
    class GlRenderTargetCache;
    class GlTextureCache;

    /**
     * @brief What a command that runs on the OpenGL ES thread can use
     */
    struct GlRenderContext {
        GlRenderTargetCache &renderTargets;
        GlTextureCache &textures; //!< The textures that the guest CPU writes (uploaded from the guest memory)
    };

    /**
     * @brief The commands that the OpenGL ES backend gets from the thread of the guest GPU: the OpenGL ES context belongs to the thread that presents frames, so the backend writes what to do
     *        (with a copy of everything it read from the guest registers) and that thread runs it before it presents a frame
     * @note This is how the first stage of the OpenGL ES backend works (item E12, phase 3): the commands run once per presented frame. Later the OpenGL ES backend gets a thread of its own that runs them as they arrive
     */
    class GlCommandQueue {
      private:
        static constexpr size_t MaxCommands{20000}; //!< What is queued past this is dropped (the thread that presents frames isn't running them), so memory doesn't grow without limit

        std::mutex mutex;
        std::vector<std::function<void(GlRenderContext &)>> commands;
        bool reportedDrop{};

      public:
        static GlCommandQueue &Get();

        /**
         * @note Called from the thread of the guest GPU
         */
        void Push(std::function<void(GlRenderContext &)> &&command);

        /**
         * @brief Runs every command that was queued, in order
         * @note The OpenGL ES context **must** be current on the calling thread
         */
        void Drain(GlRenderContext &context);
    };
}
