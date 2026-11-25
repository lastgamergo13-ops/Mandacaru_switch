// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <common.h>

namespace skyline::gpu::gl {
    /**
     * @brief The GLSL ES that is made from the shaders of a pipeline (SPIRV-Cross over the SPIR-V that the shader compiler makes for Vulkan) is compiled and linked with the OpenGL ES driver, and the results go to
     *        the log with the errors of the driver grouped so the most common ones show first (item E12, phase 4)
     * @note A pipeline is a unit: the stages are linked together, so the interface between them (the locations of what a stage writes and the next one reads) is checked too
     */
    class GlShaderTest {
      public:
        /**
         * @brief The GLSL of one stage of a pipeline
         */
        struct StageSource {
            vk::ShaderStageFlagBits stage;
            u64 hash; //!< The hash of the guest shader
            std::string source;
            std::string emitError; //!< If the GLSL couldn't be made, what went wrong (the source is empty)
        };

      private:
        struct Pending {
            u64 pipelineId;
            std::vector<StageSource> stages;
        };

        static constexpr size_t MaxPending{32};
        static constexpr size_t MaxLoggedFailures{12}; //!< The first failures are logged in full (error and the line of the source that has it)

        std::atomic<bool> enabled{};
        std::mutex mutex;
        std::deque<Pending> pending;

        // Only used by the OpenGL ES thread
        u64 compiledShaders{}, failedShaders{}, linkedPrograms{}, failedPrograms{};
        u32 loggedFailures{};
        std::map<std::string, u64> errorCounts; //!< The first line of the error of the driver (without the position) -> how many shaders or programs had it
        std::unordered_map<u64, u32> programs; //!< The linked programs by the id of the pipeline
        i64 lastSummaryMs{};

        /**
         * @return The shader object, 0 if it didn't compile (the failure is counted and logged)
         */
        u32 Compile(u64 pipelineId, const StageSource &stage);

        void Link(const Pending &pipeline);

        void CountError(const std::string &log);

        void LogSummary();

      public:
        static GlShaderTest &Get();

        void Enable();

        bool IsEnabled() const {
            return enabled.load(std::memory_order_relaxed);
        }

        /**
         * @brief Queues the stages of a pipeline to be compiled and linked
         * @note Can be called from any thread
         */
        void SubmitPipeline(u64 pipelineId, std::vector<StageSource> stages);

        /**
         * @brief Compiles and links a few of the queued pipelines and writes the summary to the log once in a while
         * @note The OpenGL ES context **must** be current on the calling thread
         */
        void Process();

        /**
         * @return The linked program of a pipeline, 0 if there isn't one (it wasn't processed yet or it failed)
         * @note The OpenGL ES context **must** be current on the calling thread
         */
        u32 GetProgram(u64 pipelineId) const;

        /**
         * @brief Changes the GLSL for desktop OpenGL 4.6 to something OpenGL ES 3.2 can take: the version, the precision of every type and the desktop only extensions (the GLSL of SPIRV-Cross needs nothing)
         */
        static std::string AdaptToEs(const std::string &source);
    };
}
