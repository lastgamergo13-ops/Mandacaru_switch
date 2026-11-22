// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <cstring>
#include <GLES3/gl32.h>
#include <common.h>
#include "texture_gallery.h"

namespace skyline::gpu::gl {
    TextureGallery &TextureGallery::Get() {
        static TextureGallery gallery;
        return gallery;
    }

    void TextureGallery::Enable() {
        enabled.store(true, std::memory_order_relaxed);
    }

    void TextureGallery::Submit(const GuestTexture &guest) {
        constexpr u32 MinimumSize{16}; // The 1x1 and 4x4 textures of a game aren't worth looking at
        constexpr size_t MaximumBytes{4 * 1024 * 1024};

        if (!guest.format || guest.dimensions.width < MinimumSize || guest.dimensions.height < MinimumSize)
            return;

        u32 layers;
        switch (guest.viewType) {
            case vk::ImageViewType::e2D:
                layers = 1;
                break;
            case vk::ImageViewType::e2DArray:
                layers = std::max(guest.layerCount, 1U);
                break;
            case vk::ImageViewType::eCube:
                layers = 6;
                break;
            default:
                return; // 1D, 3D and cube arrays aren't handled by GlTexture yet
        }

        if (!GetGlFormat(guest.format->vkFormat)) {
            std::scoped_lock lock{mutex};
            if (std::ranges::find(reportedFormats, guest.format->vkFormat) == reportedFormats.end()) {
                reportedFormats.push_back(guest.format->vkFormat);
                LOGINF("OpenGL ES gallery: the texture format {} isn't handled by the OpenGL ES backend yet", vk::to_string(guest.format->vkFormat));
            }
            return;
        }

        GuestTexture copy{guest};
        size_t size{static_cast<size_t>(copy.GetLayerStride()) * layers};
        if (size == 0 || size > MaximumBytes)
            return;

        GalleryEntry entry{.number = 0, .guest = std::move(copy)};
        entry.data.resize(size);

        // The guest memory of a texture can be split in several mappings, they are joined in order
        size_t copied{};
        for (const auto &mapping : guest.mappings) {
            if (copied >= size)
                break;
            size_t count{std::min(mapping.size(), size - copied)};
            std::memcpy(entry.data.data() + copied, mapping.data(), count);
            copied += count;
        }
        if (copied < size)
            return;

        std::scoped_lock lock{mutex};
        entry.number = counter++;
        pending.push_back(std::move(entry));
        if (pending.size() > MaxPending)
            pending.pop_front();
    }

    bool TextureGallery::TakePending(GalleryEntry &entry) {
        std::scoped_lock lock{mutex};
        if (pending.empty())
            return false;
        entry = std::move(pending.front());
        pending.pop_front();
        return true;
    }

    namespace {
        u32 CompileShader(GLenum type, const char *source) {
            u32 shader{glCreateShader(type)};
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            GLint status{};
            glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if (!status) {
                GLint length{};
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
                std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
                glGetShaderInfoLog(shader, length, nullptr, log.data());
                glDeleteShader(shader);
                throw exception("Compiling the {} shader of the texture gallery failed: {}", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
            }
            return shader;
        }
    }

    GalleryView::GalleryView() {
        constexpr const char *vertexSource{R"(#version 320 es
precision highp float;
uniform vec4 uRect; // The corners of the rectangle in normalized device coordinates: left, bottom, right, top
out vec2 vUv;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    gl_Position = vec4(mix(uRect.xy, uRect.zw, corner), 0.0, 1.0);
    vUv = vec2(corner.x, 1.0 - corner.y); // The first row of a guest texture is its top
}
)"};
        constexpr const char *fragmentSource{R"(#version 320 es
precision highp float;
uniform sampler2D uTexture;
uniform float uSrgb; // 1.0 for sRGB textures: the sampler gives linear values and the screen isn't sRGB
in vec2 vUv;
out vec4 fragColor;
void main() {
    // The alpha of the texture is shown over a checkerboard, so masks and transparent parts of the UI textures can be told apart from black and white pixels
    vec4 texel = texture(uTexture, vUv);
    vec3 color = uSrgb > 0.5 ? pow(texel.rgb, vec3(1.0 / 2.2)) : texel.rgb;
    vec2 cell = floor(gl_FragCoord.xy / 8.0);
    vec3 background = vec3(mix(0.25, 0.45, mod(cell.x + cell.y, 2.0)));
    fragColor = vec4(mix(background, color, clamp(texel.a, 0.0, 1.0)), 1.0);
}
)"};

        u32 vertex{CompileShader(GL_VERTEX_SHADER, vertexSource)}, fragment{CompileShader(GL_FRAGMENT_SHADER, fragmentSource)};
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);

        GLint status{};
        glGetProgramiv(program, GL_LINK_STATUS, &status);
        if (!status) {
            GLint length{};
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            glDeleteProgram(program);
            throw exception("Linking the program of the texture gallery failed: {}", log);
        }

        rectLocation = glGetUniformLocation(program, "uRect");
        textureLocation = glGetUniformLocation(program, "uTexture");
        srgbLocation = glGetUniformLocation(program, "uSrgb");
        LOGINF("OpenGL ES: the texture gallery shows the textures of the game (uploaded by the OpenGL ES texture code), a program of GLSL ES 3.20 was compiled and linked");
    }

    GalleryView::~GalleryView() {
        for (auto &slot : slots)
            slot.texture.reset(); // Needs the context, which is current while the view is destroyed
        if (program)
            glDeleteProgram(program);
    }

    void GalleryView::Show(const GalleryEntry &entry) {
        std::unique_ptr<GlTexture> texture;
        try {
            texture = std::make_unique<GlTexture>(entry.guest);
            texture->Upload(entry.data.data());
        } catch (const exception &e) {
            LOGINF("OpenGL ES gallery: texture #{} {}x{} {} couldn't be uploaded: {}", entry.number, entry.guest.dimensions.width, entry.guest.dimensions.height, vk::to_string(entry.guest.format->vkFormat), e.what());
            return;
        }

        size_t slotIndex{nextSlot};
        nextSlot = (nextSlot + 1) % slots.size();

        const char *shown{texture->Target() != GL_TEXTURE_2D ? ", uploaded but not shown (array or cube)" : texture->Format().isInteger ? ", uploaded but not shown (integer format)" : ""};
        LOGINF("OpenGL ES gallery: slot {} (row {}, column {}) <- texture #{} {}x{} {}, {} level(s), {} layer(s), tiling: {}{}", slotIndex, slotIndex / Columns, slotIndex % Columns, entry.number, texture->Width(), texture->Height(),
               vk::to_string(entry.guest.format->vkFormat), texture->Levels(), texture->Layers(),
               entry.guest.tileConfig.mode == texture::TileMode::Block ? "block linear" : entry.guest.tileConfig.mode == texture::TileMode::Pitch ? "pitch" : "linear", shown);

        slots[slotIndex].texture = std::move(texture);
        slots[slotIndex].shownLevel = ~0U;
    }

    void GalleryView::Draw(i32 width, i32 height, u64 frame, bool overlay) {
        constexpr size_t UploadsPerFrame{2};

        GalleryEntry entry;
        for (size_t i{}; i < UploadsPerFrame && TextureGallery::Get().TakePending(entry); i++)
            Show(entry);

        // The grid fits in a region of the screen: all of it, or the top 30% when it's drawn over a frame
        i32 regionY{overlay ? height - height * 3 / 10 : 0}, regionHeight{overlay ? height * 3 / 10 : height};
        glViewport(0, regionY, width, regionHeight);
        glDisable(GL_SCISSOR_TEST);
        if (!overlay) {
            glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        glUseProgram(program);
        glUniform1i(textureLocation, 0);
        glActiveTexture(GL_TEXTURE0);

        constexpr float Padding{0.02f}; // In normalized device coordinates, so it is a fraction of the cell
        float cellWidth{2.0f / Columns}, cellHeight{2.0f / Rows};
        for (size_t i{}; i < slots.size(); i++) {
            auto &slot{slots[i]};
            if (!slot.texture || slot.texture->Target() != GL_TEXTURE_2D || slot.texture->Format().isInteger)
                continue;

            // The level that is shown changes over time, the image is stretched to the size of the first level so the levels can be compared
            u32 level{static_cast<u32>((frame / FramesPerLevel + i) % slot.texture->Levels())};
            glBindTexture(GL_TEXTURE_2D, slot.texture->Name());
            if (level != slot.shownLevel) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, static_cast<GLint>(level));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(level));
                slot.shownLevel = level;
            }

            float textureWidth{static_cast<float>(slot.texture->Width())}, textureHeight{static_cast<float>(slot.texture->Height())};
            float column{static_cast<float>(i % Columns)}, row{static_cast<float>(i / Columns)};
            float cellX{-1.0f + column * cellWidth}, cellY{1.0f - (row + 1.0f) * cellHeight};

            // The texture is scaled to fit in the cell without changing its aspect ratio (in pixels, not in normalized device coordinates)
            float cellPixelsWidth{static_cast<float>(width) / Columns * (1.0f - Padding * 2)}, cellPixelsHeight{static_cast<float>(regionHeight) / Rows * (1.0f - Padding * 2)};
            float scale{std::min(cellPixelsWidth / textureWidth, cellPixelsHeight / textureHeight)};
            float drawWidth{textureWidth * scale / static_cast<float>(width) * 2.0f}, drawHeight{textureHeight * scale / static_cast<float>(regionHeight) * 2.0f};
            float x0{cellX + (cellWidth - drawWidth) / 2.0f}, y0{cellY + (cellHeight - drawHeight) / 2.0f};

            glUniform1f(srgbLocation, slot.texture->Format().isSrgb ? 1.0f : 0.0f);
            glUniform4f(rectLocation, x0, y0, x0 + drawWidth, y0 + drawHeight);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        // A white bar at the bottom that moves 8 pixels per frame shows that frames are being presented and at which rate
        glEnable(GL_SCISSOR_TEST);
        glScissor(static_cast<GLint>((frame * 8) % static_cast<u64>(std::max(width, 1))), 0, 16, 6);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
    }
}
