// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <array>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <common.h>
#include "egl_context.h"

namespace skyline::gpu::gl {
    bool GlInfo::HasExtension(std::string_view name) const {
        return std::ranges::any_of(extensions, [&](const std::string &extension) { return extension == name; });
    }

    EglContext::EglContext(ANativeWindow *window) {
        try {
            display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
            if (display == EGL_NO_DISPLAY)
                throw exception("eglGetDisplay returned no display: 0x{:X}", eglGetError());

            EGLint eglMajor, eglMinor;
            if (!eglInitialize(display, &eglMajor, &eglMinor))
                throw exception("eglInitialize failed: 0x{:X}", eglGetError());

            const EGLint configAttributes[]{
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_NONE,
            };
            EGLint configCount{};
            if (!eglChooseConfig(display, configAttributes, &config, 1, &configCount) || configCount < 1)
                throw exception("No EGL config supports OpenGL ES 3 with a RGBA8 window surface (error 0x{:X})", eglGetError());

            if (!eglBindAPI(EGL_OPENGL_ES_API))
                throw exception("eglBindAPI(OpenGL ES) failed: 0x{:X}", eglGetError());

            // Ask for 3.2 first, a driver that can't give it fails the creation and then 3.0 is asked for so the version it does have can be reported
            const EGLint context32Attributes[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 2, EGL_NONE};
            context = eglCreateContext(display, config, EGL_NO_CONTEXT, context32Attributes);
            if (context == EGL_NO_CONTEXT) {
                LOGW("Creating an OpenGL ES 3.2 context failed (0x{:X}), trying 3.0", eglGetError());
                const EGLint context30Attributes[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
                context = eglCreateContext(display, config, EGL_NO_CONTEXT, context30Attributes);
            }
            if (context == EGL_NO_CONTEXT)
                throw exception("Creating an OpenGL ES 3 context failed: 0x{:X}", eglGetError());

            surface = eglCreateWindowSurface(display, config, reinterpret_cast<EGLNativeWindowType>(window), nullptr);
            if (surface == EGL_NO_SURFACE)
                throw exception("Creating the EGL window surface failed: 0x{:X}", eglGetError());
        } catch (...) {
            Destroy();
            throw;
        }
    }

    void EglContext::Destroy() {
        if (display == EGL_NO_DISPLAY)
            return;

        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE)
            eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT)
            eglDestroyContext(display, context);
        eglTerminate(display);

        surface = EGL_NO_SURFACE;
        context = EGL_NO_CONTEXT;
        display = EGL_NO_DISPLAY;
    }

    EglContext::~EglContext() {
        Destroy();
    }

    void EglContext::MakeCurrent() {
        if (!eglMakeCurrent(display, surface, surface, context))
            throw exception("eglMakeCurrent failed: 0x{:X}", eglGetError());
    }

    void EglContext::ReleaseCurrent() {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    void EglContext::SwapBuffers() {
        if (!eglSwapBuffers(display, surface))
            throw exception("eglSwapBuffers failed: 0x{:X}", eglGetError());
    }

    void EglContext::SetSwapInterval(i32 interval) {
        if (!eglSwapInterval(display, interval))
            LOGW("eglSwapInterval({}) failed: 0x{:X}", interval, eglGetError());
    }

    std::pair<i32, i32> EglContext::GetSize() const {
        EGLint width{}, height{};
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);
        return {width, height};
    }

    GlInfo EglContext::QueryInfo() {
        auto getString{[](GLenum name) -> std::string {
            const auto *value{reinterpret_cast<const char *>(glGetString(name))};
            return value ? value : "";
        }};

        GlInfo info;
        info.vendor = getString(GL_VENDOR);
        info.renderer = getString(GL_RENDERER);
        info.version = getString(GL_VERSION);
        info.shadingLanguageVersion = getString(GL_SHADING_LANGUAGE_VERSION);
        glGetIntegerv(GL_MAJOR_VERSION, &info.major);
        glGetIntegerv(GL_MINOR_VERSION, &info.minor);

        GLint extensionCount{};
        glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
        for (GLint i{}; i < extensionCount; i++) {
            const auto *extension{reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)))};
            if (extension)
                info.extensions.emplace_back(extension);
        }

        return info;
    }

    void EglContext::LogInfo(const GlInfo &info) {
        LOGINF("OpenGL ES: vendor: \"{}\", renderer: \"{}\", version: \"{}\" ({}.{}), GLSL: \"{}\", {} extension(s)", info.vendor, info.renderer, info.version, info.major, info.minor, info.shadingLanguageVersion, info.extensions.size());

        struct Limit {
            GLenum name;
            const char *label;
        };
        constexpr std::array limits{
            Limit{GL_MAX_TEXTURE_SIZE, "max texture size"},
            Limit{GL_MAX_ARRAY_TEXTURE_LAYERS, "max array texture layers"},
            Limit{GL_MAX_COLOR_ATTACHMENTS, "max color attachments"},
            Limit{GL_MAX_DRAW_BUFFERS, "max draw buffers"},
            Limit{GL_MAX_SAMPLES, "max samples"},
            Limit{GL_MAX_VERTEX_ATTRIBS, "max vertex attributes"},
            Limit{GL_MAX_UNIFORM_BLOCK_SIZE, "max uniform block size"},
            Limit{GL_MAX_UNIFORM_BUFFER_BINDINGS, "max uniform buffer bindings"},
            Limit{GL_MAX_SHADER_STORAGE_BLOCK_SIZE, "max shader storage block size"},
            Limit{GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, "max shader storage buffer bindings"},
            Limit{GL_MAX_TEXTURE_IMAGE_UNITS, "max texture image units (fragment)"},
            Limit{GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, "max combined texture image units"},
            Limit{GL_MAX_IMAGE_UNITS, "max image units"},
            Limit{GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, "max compute work group invocations"},
            Limit{GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, "max compute shared memory size"},
            Limit{GL_MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS, "max transform feedback separate attributes"},
        };

        std::string limitsLine;
        for (const auto &limit : limits) {
            GLint value{};
            glGetIntegerv(limit.name, &value);
            limitsLine += fmt::format("{}{}: {}", limitsLine.empty() ? "" : ", ", limit.label, value);
        }
        LOGINF("OpenGL ES limits: {}", limitsLine);

        // The extensions the backend could use to cover what the Maxwell GPU does and OpenGL ES 3.2 doesn't (see section 5 of the OpenGL report)
        constexpr std::array interesting{
            "GL_EXT_texture_compression_s3tc", "GL_EXT_texture_compression_bptc", "GL_EXT_texture_compression_rgtc", "GL_KHR_texture_compression_astc_ldr", "GL_KHR_texture_compression_astc_hdr",
            "GL_EXT_buffer_storage", "GL_EXT_clip_control", "GL_EXT_base_instance", "GL_EXT_depth_clamp", "GL_OES_viewport_array", "GL_NV_viewport_array2", "GL_EXT_blend_func_extended",
            "GL_EXT_texture_filter_anisotropic", "GL_EXT_texture_filter_minmax", "GL_EXT_texture_mirror_clamp_to_edge", "GL_EXT_polygon_offset_clamp", "GL_NV_polygon_mode",
            "GL_EXT_provoking_vertex", "GL_EXT_shader_framebuffer_fetch", "GL_KHR_debug", "GL_EXT_disjoint_timer_query", "GL_KHR_robustness", "GL_EXT_texture_view",
        };
        std::string interestingLine;
        for (const auto *name : interesting)
            interestingLine += fmt::format("{}{}: {}", interestingLine.empty() ? "" : ", ", name, info.HasExtension(name) ? "yes" : "no");
        LOGINF("OpenGL ES extensions of interest: {}", interestingLine);

        // The full list in lines that are short enough to not be cut by the logger
        std::string line;
        for (const auto &extension : info.extensions) {
            if (!line.empty() && line.size() + extension.size() > 600) {
                LOGINF("OpenGL ES extensions: {}", line);
                line.clear();
            }
            line += (line.empty() ? "" : " ") + extension;
        }
        if (!line.empty())
            LOGINF("OpenGL ES extensions: {}", line);
    }

    bool ProbeOpenGlEs32(ANativeWindow *window) {
        try {
            EglContext context{window};
            context.MakeCurrent();
            auto info{EglContext::QueryInfo()};
            EglContext::LogInfo(info);
            context.ReleaseCurrent();

            if (!info.IsEs32OrNewer()) {
                LOGW("This device has OpenGL ES {}.{}, the OpenGL ES backend needs 3.2: it can't be used", info.major, info.minor);
                return false;
            }
            return true;
        } catch (const std::exception &e) {
            LOGW("The OpenGL ES context couldn't be created, the OpenGL ES backend can't be used: {}", e.what());
            return false;
        }
    }

    void DrawTestPattern(i32 width, i32 height, u64 frame) {
        constexpr std::array<std::array<float, 3>, 8> bars{{
            {1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 0.0f},
            {0.0f, 1.0f, 1.0f},
            {0.0f, 1.0f, 0.0f},
            {1.0f, 0.0f, 1.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 0.0f},
        }};

        glViewport(0, 0, width, height);
        glEnable(GL_SCISSOR_TEST);

        const i32 barWidth{std::max(width / static_cast<i32>(bars.size()), 1)};
        for (size_t i{}; i < bars.size(); i++) {
            i32 x{static_cast<i32>(i) * barWidth};
            i32 w{i + 1 == bars.size() ? std::max(width - x, 0) : barWidth}; // The last bar takes the pixels that are left
            glScissor(x, 0, w, height);
            glClearColor(bars[i][0] * 0.75f, bars[i][1] * 0.75f, bars[i][2] * 0.75f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        // A white bar that moves 8 pixels per frame, it shows that the frames are being presented and at which rate
        constexpr i32 MovingBarWidth{16};
        i32 movingX{static_cast<i32>((frame * 8) % static_cast<u64>(std::max(width, 1)))};
        glScissor(movingX, height / 3, MovingBarWidth, height / 3);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glDisable(GL_SCISSOR_TEST);
    }
}
