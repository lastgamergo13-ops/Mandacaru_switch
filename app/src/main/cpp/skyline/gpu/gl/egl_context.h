// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string>
#include <vector>
#include <EGL/egl.h>
#include <common.h>

struct ANativeWindow;

namespace skyline::gpu::gl {
    /**
     * @brief What the OpenGL ES driver of the device reports, it is logged so the next steps of the OpenGL backend know what the device can do (item E12 of the report)
     */
    struct GlInfo {
        std::string vendor;
        std::string renderer;
        std::string version;
        std::string shadingLanguageVersion;
        i32 major{}, minor{};
        std::vector<std::string> extensions;

        /**
         * @return If the version is OpenGL ES 3.2 or newer, the minimum the backend needs
         */
        bool IsEs32OrNewer() const {
            return major > 3 || (major == 3 && minor >= 2);
        }

        bool HasExtension(std::string_view name) const;
    };

    /**
     * @brief An EGL display, OpenGL ES 3.2 context and window surface for an ANativeWindow
     * @note A context can be current on only one thread at a time: it is made current on the thread that presents frames and released before being destroyed
     */
    class EglContext {
      private:
        EGLDisplay display{EGL_NO_DISPLAY};
        EGLConfig config{};
        EGLContext context{EGL_NO_CONTEXT};
        EGLSurface surface{EGL_NO_SURFACE};

        void Destroy();

      public:
        /**
         * @brief Creates the display, the context (OpenGL ES 3.2 if the driver has it, otherwise 3.0 so it can be reported) and the window surface
         * @note Throws skyline::exception if any of that fails, nothing is left behind in that case
         */
        EglContext(ANativeWindow *window);

        ~EglContext();

        EglContext(const EglContext &) = delete;

        EglContext &operator=(const EglContext &) = delete;

        void MakeCurrent();

        void ReleaseCurrent();

        void SwapBuffers();

        /**
         * @param interval The number of display refreshes between buffer swaps, 0 doesn't wait for the display
         */
        void SetSwapInterval(i32 interval);

        /**
         * @return The size of the window surface in pixels
         */
        std::pair<i32, i32> GetSize() const;

        /**
         * @brief Reads what the driver reports (vendor, renderer, version, extensions)
         * @note The context **must** be current on the calling thread
         */
        static GlInfo QueryInfo();

        /**
         * @brief Writes the driver information, the limits the backend cares about and which of the extensions it could use are present to the log
         * @note The context **must** be current on the calling thread
         */
        static void LogInfo(const GlInfo &info);
    };

    /**
     * @brief Tries to create an OpenGL ES context on the window, logs what the driver reports and destroys it again
     * @return If the device can run the OpenGL ES backend (an OpenGL ES 3.2 context could be created on the window)
     * @note Used when the window is received to decide between OpenGL ES and falling back to Vulkan, the context that is actually used is created by the thread that presents frames
     */
    bool ProbeOpenGlEs32(ANativeWindow *window);

    /**
     * @brief Draws a test pattern (colour bars with a moving white bar) to the current framebuffer, it's what the OpenGL ES backend shows while it can't render the guest yet
     * @note The context **must** be current on the calling thread
     */
    void DrawTestPattern(i32 width, i32 height, u64 frame);
}
