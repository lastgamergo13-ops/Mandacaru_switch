// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <chrono>
#include <GLES3/gl32.h>
#include <fmt/format.h>
#include "gl_shader_test.h"

namespace skyline::gpu::gl {
    GlShaderTest &GlShaderTest::Get() {
        static GlShaderTest test;
        return test;
    }

    void GlShaderTest::Enable() {
        enabled.store(true, std::memory_order_relaxed);
    }

    void GlShaderTest::SubmitPipeline(u64 pipelineId, std::vector<StageSource> stages) {
        std::scoped_lock lock{mutex};
        if (pending.size() >= MaxPending)
            pending.pop_front(); // The newest pipelines are the ones that matter, the thread that compiles can't keep up with a game that creates many
        pending.push_back({pipelineId, std::move(stages)});
    }

    u32 GlShaderTest::GetProgram(u64 pipelineId) const {
        auto it{programs.find(pipelineId)};
        return it != programs.end() ? it->second : 0;
    }

    std::string GlShaderTest::AdaptToEs(const std::string &source) {
        if (source.starts_with("#version 320 es"))
            return source; // SPIRV-Cross already makes GLSL ES 3.20 with its own precisions

        // Every sampler and image type needs a precision in OpenGL ES (the ones that have a default are only the first few), declared once at the top
        constexpr const char *Header{
            "#version 320 es\n"
            "precision highp float;\n"
            "precision highp int;\n"
            "precision highp sampler2D;\nprecision highp sampler3D;\nprecision highp samplerCube;\nprecision highp sampler2DArray;\n"
            "precision highp sampler2DShadow;\nprecision highp samplerCubeShadow;\nprecision highp sampler2DArrayShadow;\n"
            "precision highp isampler2D;\nprecision highp isampler3D;\nprecision highp isamplerCube;\nprecision highp isampler2DArray;\n"
            "precision highp usampler2D;\nprecision highp usampler3D;\nprecision highp usamplerCube;\nprecision highp usampler2DArray;\n"
            "precision highp samplerBuffer;\nprecision highp isamplerBuffer;\nprecision highp usamplerBuffer;\n"
            "precision highp image2D;\nprecision highp image3D;\nprecision highp imageCube;\nprecision highp image2DArray;\n"
            "precision highp iimage2D;\nprecision highp iimage3D;\nprecision highp iimage2DArray;\n"
            "precision highp uimage2D;\nprecision highp uimage3D;\nprecision highp uimage2DArray;\nprecision highp uimageBuffer;\n"
        };

        std::string adapted{Header};
        size_t position{};
        bool first{true};
        while (position < source.size()) {
            size_t end{source.find('\n', position)};
            end = end == std::string::npos ? source.size() : end + 1;
            std::string_view line{std::string_view{source}.substr(position, end - position)};
            position = end;

            if (first) {
                first = false;
                if (line.starts_with("#version"))
                    continue; // Replaced by the header
            }

            // The extensions of desktop OpenGL aren't known to OpenGL ES (using what they give is an error later, this only keeps the warnings out of the log)
            if (line.starts_with("#extension GL_ARB_") || line.starts_with("#extension GL_NV_") || line.starts_with("#extension GL_AMD_"))
                continue;

            adapted += line;
        }
        return adapted;
    }

    void GlShaderTest::CountError(const std::string &log) {
        // The key of the summary is the first error without the position ("0:123(45): error: ..." -> "error: ...")
        size_t firstError{log.find("rror")};
        std::string key{firstError == std::string::npos ? log.substr(0, 120) : log.substr(firstError, std::min<size_t>(log.find('\n', firstError) - firstError, 160))};
        errorCounts[key]++;
    }

    u32 GlShaderTest::Compile(u64 pipelineId, const StageSource &stage) {
        if (!stage.emitError.empty()) {
            failedShaders++;
            errorCounts["GLSL conversion: " + stage.emitError.substr(0, 150)]++;
            if (loggedFailures < MaxLoggedFailures) {
                loggedFailures++;
                LOGINF("OpenGL ES pipeline {}: the GLSL of the shader {:016X} ({}) couldn't be made: {}", pipelineId, stage.hash, vk::to_string(stage.stage), stage.emitError.substr(0, 400));
            }
            return 0;
        }

        GLenum type;
        switch (stage.stage) {
            case vk::ShaderStageFlagBits::eVertex:
                type = GL_VERTEX_SHADER;
                break;
            case vk::ShaderStageFlagBits::eTessellationControl:
                type = GL_TESS_CONTROL_SHADER;
                break;
            case vk::ShaderStageFlagBits::eTessellationEvaluation:
                type = GL_TESS_EVALUATION_SHADER;
                break;
            case vk::ShaderStageFlagBits::eGeometry:
                type = GL_GEOMETRY_SHADER;
                break;
            case vk::ShaderStageFlagBits::eFragment:
                type = GL_FRAGMENT_SHADER;
                break;
            default:
                return 0;
        }

        std::string adapted{AdaptToEs(stage.source)};
        const char *text{adapted.c_str()};
        GLuint name{glCreateShader(type)};
        glShaderSource(name, 1, &text, nullptr);
        glCompileShader(name);

        GLint status{};
        glGetShaderiv(name, GL_COMPILE_STATUS, &status);
        if (status) {
            compiledShaders++;
            return name;
        }

        failedShaders++;
        GLint length{};
        glGetShaderiv(name, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(name, length, nullptr, log.data());
        CountError(log);

        if (loggedFailures < MaxLoggedFailures) {
            loggedFailures++;

            // The line of the source that has the first error: the log has "0:<line>"
            std::string line;
            size_t colon{log.find("0:")};
            if (colon != std::string::npos) {
                size_t lineNumber{static_cast<size_t>(std::strtoul(log.c_str() + colon + 2, nullptr, 10))};
                size_t start{};
                for (size_t i{1}; i < lineNumber && start != std::string::npos; i++) {
                    start = adapted.find('\n', start);
                    if (start != std::string::npos)
                        start++;
                }
                if (start != std::string::npos)
                    line = adapted.substr(start, std::min<size_t>(adapted.find('\n', start) - start, 240));
            }

            LOGINF("OpenGL ES pipeline {}: shader {:016X} ({}) failed to compile: {} | line: {}", pipelineId, stage.hash, vk::to_string(stage.stage), log.substr(0, 400), line);
        }
        glDeleteShader(name);
        return 0;
    }

    void GlShaderTest::Link(const Pending &pipeline) {
        std::vector<GLuint> shaders;
        bool allCompiled{true};
        for (const auto &stage : pipeline.stages) {
            if (auto shader{Compile(pipeline.pipelineId, stage)})
                shaders.push_back(shader);
            else
                allCompiled = false;
        }

        if (!allCompiled || shaders.empty()) {
            failedPrograms++;
            for (auto shader : shaders)
                glDeleteShader(shader);
            return;
        }

        GLuint program{glCreateProgram()};
        for (auto shader : shaders)
            glAttachShader(program, shader);
        glLinkProgram(program);
        for (auto shader : shaders) {
            glDetachShader(program, shader);
            glDeleteShader(shader);
        }

        GLint status{};
        glGetProgramiv(program, GL_LINK_STATUS, &status);
        if (status) {
            linkedPrograms++;
            programs[pipeline.pipelineId] = program;
            return;
        }

        failedPrograms++;
        GLint length{};
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        CountError("link: " + log);
        if (loggedFailures < MaxLoggedFailures) {
            loggedFailures++;
            std::string stages;
            for (const auto &stage : pipeline.stages)
                stages += fmt::format("{}{}", stages.empty() ? "" : ", ", vk::to_string(stage.stage));
            LOGINF("OpenGL ES pipeline {} ({}) failed to link: {}", pipeline.pipelineId, stages, log.substr(0, 400));
        }
        glDeleteProgram(program);
    }

    void GlShaderTest::LogSummary() {
        auto nowMs{std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()};
        if (nowMs - lastSummaryMs < 10000)
            return;
        lastSummaryMs = nowMs;

        if (compiledShaders + failedShaders + linkedPrograms + failedPrograms == 0)
            return;

        LOGINF("OpenGL ES shaders so far: {} compiled, {} failed; pipelines: {} linked, {} failed", compiledShaders, failedShaders, linkedPrograms, failedPrograms);

        // The most common errors first
        std::vector<std::pair<std::string, u64>> sorted(errorCounts.begin(), errorCounts.end());
        std::ranges::sort(sorted, [](const auto &a, const auto &b) { return a.second > b.second; });
        for (size_t i{}; i < std::min<size_t>(sorted.size(), 8); i++)
            LOGINF("OpenGL ES shader error x{}: {}", sorted[i].second, sorted[i].first);
    }

    void GlShaderTest::Process() {
        constexpr size_t PipelinesPerCall{2};

        for (size_t i{}; i < PipelinesPerCall; i++) {
            Pending pipeline;
            {
                std::scoped_lock lock{mutex};
                if (pending.empty())
                    break;
                pipeline = std::move(pending.front());
                pending.pop_front();
            }
            Link(pipeline);
        }

        LogSummary();
    }
}
