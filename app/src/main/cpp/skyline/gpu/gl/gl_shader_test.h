// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vulkan/vulkan.hpp>
#include <common.h>

namespace skyline::gpu::gl {
    /**
     * @brief Takes the GLSL that the shader compiler makes of the guest shaders and compiles it with the OpenGL ES driver, to find out what the GLSL backend of the compiler needs to work on OpenGL ES 3.2
     *        (item E12, phase 4): the results go to the log, with the errors of the driver grouped so the most common ones show first
     * @note The GLSL backend targets desktop OpenGL (#version 460 and ARB/NV extensions): the source is adapted to OpenGL ES before it's compiled (see AdaptToEs), and what can't be adapted shows up as errors
     */
    class GlShaderTest {
      private:
        struct Pending {
            vk::ShaderStageFlagBits stage;
            u64 hash;
            std::string source;
            std::string emitError; //!< If the compiler couldn't make the GLSL, what it said (the source is empty)
        };

        static constexpr size_t MaxPending{64};
        static constexpr size_t MaxLoggedFailures{12}; //!< The first failures are logged in full (error and the line of the source that has it)

        std::atomic<bool> enabled{};
        std::mutex mutex;
        std::deque<Pending> pending;

        // Only used by the OpenGL ES thread
        u64 compiled{}, failed{};
        u32 loggedFailures{};
        std::map<std::string, u64> errorCounts; //!< The first line of the error of the driver (without the position) -> how many shaders had it
        i64 lastSummaryMs{};

        void Compile(const Pending &shader);

        void LogSummary();

      public:
        static GlShaderTest &Get();

        void Enable();

        bool IsEnabled() const {
            return enabled.load(std::memory_order_relaxed);
        }

        /**
         * @brief Queues the GLSL of a stage of a pipeline
         * @note Can be called from any thread
         */
        void Submit(vk::ShaderStageFlagBits stage, u64 hash, std::string source);

        /**
         * @brief Queues that the GLSL backend of the compiler failed to make the GLSL of a stage (an instruction it doesn't implement, for example)
         * @note Can be called from any thread
         */
        void SubmitEmitFailure(vk::ShaderStageFlagBits stage, u64 hash, std::string error);

        /**
         * @brief Compiles a few of the queued shaders and writes the summary to the log once in a while
         * @note The OpenGL ES context **must** be current on the calling thread
         */
        void Process();

        /**
         * @brief Changes the GLSL for desktop OpenGL 4.6 that the compiler makes to something OpenGL ES 3.2 can take: the version, the precision of every type and the desktop only extensions
         */
        static std::string AdaptToEs(const std::string &source);
    };
}
