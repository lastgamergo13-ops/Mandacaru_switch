// SPDX-License-Identifier: MPL-2.0

#include <common.h>
#include "gl_command_queue.h"

namespace skyline::gpu::gl {
    GlCommandQueue &GlCommandQueue::Get() {
        static GlCommandQueue queue;
        return queue;
    }

    void GlCommandQueue::Push(std::function<void(GlRenderContext &)> &&command) {
        std::scoped_lock lock{mutex};
        if (commands.size() >= MaxCommands) {
            if (!reportedDrop) {
                reportedDrop = true;
                LOGW("The OpenGL ES command queue is full ({} commands), the commands are dropped until the thread that presents frames runs them", MaxCommands);
            }
            return;
        }
        reportedDrop = false;
        commands.push_back(std::move(command));
    }

    void GlCommandQueue::Drain(GlRenderContext &context) {
        std::vector<std::function<void(GlRenderContext &)>> pending;
        {
            std::scoped_lock lock{mutex};
            pending.swap(commands);
        }

        for (auto &command : pending)
            command(context);
    }
}
