// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <cmath>
#include <cstring>
#include <GLES3/gl32.h>
#include <common.h>
#include <gpu/texture/layout.h>
#include <gpu/texture/bc_decoder.h>
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
        constexpr size_t MaximumBytes{8 * 1024 * 1024};

        if (!guest.format || guest.dimensions.width < MinimumSize || guest.dimensions.height < MinimumSize)
            return;
        if (guest.viewType == vk::ImageViewType::e3D || guest.viewType == vk::ImageViewType::e1D || guest.viewType == vk::ImageViewType::e1DArray)
            return; // Only 2D images (and the first layer of arrays and cubes) are decoded

        texture::Dimensions dimensions{guest.dimensions.width, guest.dimensions.height, 1};
        const auto &format{*guest.format};

        size_t size;
        switch (guest.tileConfig.mode) {
            case texture::TileMode::Block:
                size = texture::GetBlockLinearLayerSize(dimensions, format.blockWidth, format.blockHeight, format.bpb, guest.tileConfig.blockHeight, guest.tileConfig.blockDepth);
                break;
            case texture::TileMode::Pitch:
                size = static_cast<size_t>(guest.tileConfig.pitch) * dimensions.height;
                break;
            default:
                size = format.GetSize(dimensions);
                break;
        }
        if (size == 0 || size > MaximumBytes)
            return;

        GalleryEntry entry{.number = 0, .guest = guest};
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
        /**
         * @brief What the gallery has after decoding a texture: either RGBA8 pixels or blocks that the driver decodes itself
         */
        struct DecodedImage {
            u32 width{}, height{};
            std::vector<u8> pixels;
            u32 compressedFormat{}; //!< 0 when 'pixels' is RGBA8, otherwise the OpenGL ES compressed internal format
            std::string description;
        };

        constexpr u32 AstcBase{0x93B0}; // GL_COMPRESSED_RGBA_ASTC_4x4_KHR, the 14 block sizes follow in this order: 4x4 5x4 5x5 6x5 6x6 8x5 8x6 8x8 10x5 10x6 10x8 10x10 12x10 12x12
        constexpr u32 AstcSrgbBase{0x93D0}; // GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR

        /**
         * @return The index of the ASTC block size (see AstcBase), -1 if it isn't ASTC
         */
        i32 AstcIndex(vk::Format vkFormat, bool &srgb) {
            constexpr std::array<std::pair<vk::Format, vk::Format>, 14> formats{{
                {vk::Format::eAstc4x4UnormBlock, vk::Format::eAstc4x4SrgbBlock},
                {vk::Format::eAstc5x4UnormBlock, vk::Format::eAstc5x4SrgbBlock},
                {vk::Format::eAstc5x5UnormBlock, vk::Format::eAstc5x5SrgbBlock},
                {vk::Format::eAstc6x5UnormBlock, vk::Format::eAstc6x5SrgbBlock},
                {vk::Format::eAstc6x6UnormBlock, vk::Format::eAstc6x6SrgbBlock},
                {vk::Format::eAstc8x5UnormBlock, vk::Format::eAstc8x5SrgbBlock},
                {vk::Format::eAstc8x6UnormBlock, vk::Format::eAstc8x6SrgbBlock},
                {vk::Format::eAstc8x8UnormBlock, vk::Format::eAstc8x8SrgbBlock},
                {vk::Format::eAstc10x5UnormBlock, vk::Format::eAstc10x5SrgbBlock},
                {vk::Format::eAstc10x6UnormBlock, vk::Format::eAstc10x6SrgbBlock},
                {vk::Format::eAstc10x8UnormBlock, vk::Format::eAstc10x8SrgbBlock},
                {vk::Format::eAstc10x10UnormBlock, vk::Format::eAstc10x10SrgbBlock},
                {vk::Format::eAstc12x10UnormBlock, vk::Format::eAstc12x10SrgbBlock},
                {vk::Format::eAstc12x12UnormBlock, vk::Format::eAstc12x12SrgbBlock},
            }};
            for (size_t i{}; i < formats.size(); i++) {
                if (formats[i].first == vkFormat) {
                    srgb = false;
                    return static_cast<i32>(i);
                } else if (formats[i].second == vkFormat) {
                    srgb = true;
                    return static_cast<i32>(i);
                }
            }
            return -1;
        }

        float HalfToFloat(u16 half) {
            u32 sign{static_cast<u32>(half >> 15) & 1}, exponent{static_cast<u32>((half >> 10) & 0x1F)}, mantissa{half & 0x3FFU};
            float value;
            if (exponent == 0)
                value = std::ldexp(static_cast<float>(mantissa), -24);
            else if (exponent == 31)
                value = mantissa ? 0.0f : 65504.0f; // NaN is shown as black and infinity as the largest value
            else
                value = std::ldexp(static_cast<float>(mantissa | 0x400U), static_cast<i32>(exponent) - 25);
            return sign ? -value : value;
        }

        u8 ToByte(float value) {
            return static_cast<u8>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        /**
         * @brief Takes the guest layout of the first mip level and layer to linear rows of blocks and converts what comes out to something that can be shown
         */
        bool Decode(const GalleryEntry &entry, DecodedImage &image) {
            const auto &guest{entry.guest};
            const auto &format{*guest.format};
            GuestTexture level{guest};
            level.dimensions.depth = 1;
            u32 width{level.dimensions.width}, height{level.dimensions.height};

            std::vector<u8> linear(format.GetSize(level.dimensions));
            auto *input{const_cast<u8 *>(entry.data.data())};
            switch (guest.tileConfig.mode) {
                case texture::TileMode::Block:
                    texture::CopyBlockLinearToLinear(level, input, linear.data());
                    break;
                case texture::TileMode::Pitch:
                    texture::CopyPitchLinearToLinear(level, input, linear.data());
                    break;
                default:
                    std::memcpy(linear.data(), input, std::min(linear.size(), entry.data.size()));
                    break;
            }

            image.width = width;
            image.height = height;
            image.description = vk::to_string(format.vkFormat);

            // The decoders write whole blocks, the buffers are padded to a multiple of the block so a texture that isn't one can't write out of them
            size_t paddedPixels{static_cast<size_t>((width + 3) & ~3U) * ((height + 3) & ~3U)};
            auto rgba{[&](size_t bytesPerPixel) { image.pixels.assign(std::max<size_t>(paddedPixels * bytesPerPixel, size_t{width} * height * 4), 0); }};

            bool srgb;
            if (auto astc{AstcIndex(format.vkFormat, srgb)}; astc >= 0) {
                // The driver of the device decodes ASTC on its own, the blocks are given to it as they are
                image.compressedFormat = (srgb ? AstcSrgbBase : AstcBase) + static_cast<u32>(astc);
                image.pixels = std::move(linear);
                image.description += " (ASTC, decoded by the driver)";
                return true;
            }

            switch (format.vkFormat) {
                case vk::Format::eR8G8B8A8Unorm:
                case vk::Format::eR8G8B8A8Srgb:
                    image.pixels = std::move(linear);
                    return true;

                case vk::Format::eB8G8R8A8Unorm:
                case vk::Format::eB8G8R8A8Srgb:
                    image.pixels = std::move(linear);
                    for (size_t i{}; i + 3 < image.pixels.size(); i += 4)
                        std::swap(image.pixels[i], image.pixels[i + 2]);
                    return true;

                case vk::Format::eR8Unorm: {
                    image.pixels.resize(size_t{width} * height * 4);
                    for (size_t i{}; i < size_t{width} * height; i++)
                        std::memset(&image.pixels[i * 4], linear[i], 3), image.pixels[i * 4 + 3] = 255;
                    return true;
                }

                case vk::Format::eR8G8Unorm: {
                    image.pixels.resize(size_t{width} * height * 4);
                    for (size_t i{}; i < size_t{width} * height; i++) {
                        image.pixels[i * 4 + 0] = linear[i * 2];
                        image.pixels[i * 4 + 1] = linear[i * 2 + 1];
                        image.pixels[i * 4 + 2] = 0;
                        image.pixels[i * 4 + 3] = 255;
                    }
                    return true;
                }

                case vk::Format::eA2B10G10R10UnormPack32: {
                    image.pixels.resize(size_t{width} * height * 4);
                    for (size_t i{}; i < size_t{width} * height; i++) {
                        u32 value;
                        std::memcpy(&value, &linear[i * 4], 4);
                        image.pixels[i * 4 + 0] = static_cast<u8>(((value >> 0) & 0x3FF) >> 2);
                        image.pixels[i * 4 + 1] = static_cast<u8>(((value >> 10) & 0x3FF) >> 2);
                        image.pixels[i * 4 + 2] = static_cast<u8>(((value >> 20) & 0x3FF) >> 2);
                        image.pixels[i * 4 + 3] = 255;
                    }
                    return true;
                }

                case vk::Format::eR16G16B16A16Sfloat: {
                    image.pixels.resize(size_t{width} * height * 4);
                    for (size_t i{}; i < size_t{width} * height; i++) {
                        for (size_t c{}; c < 3; c++) {
                            u16 half;
                            std::memcpy(&half, &linear[i * 8 + c * 2], 2);
                            image.pixels[i * 4 + c] = ToByte(HalfToFloat(half));
                        }
                        image.pixels[i * 4 + 3] = 255;
                    }
                    return true;
                }

                case vk::Format::eBc1RgbaUnormBlock:
                case vk::Format::eBc1RgbaSrgbBlock:
                    rgba(4);
                    bcn::DecodeBc1(linear.data(), image.pixels.data(), width, height, true);
                    return true;

                case vk::Format::eBc2UnormBlock:
                case vk::Format::eBc2SrgbBlock:
                    rgba(4);
                    bcn::DecodeBc2(linear.data(), image.pixels.data(), width, height);
                    return true;

                case vk::Format::eBc3UnormBlock:
                case vk::Format::eBc3SrgbBlock:
                    rgba(4);
                    bcn::DecodeBc3(linear.data(), image.pixels.data(), width, height);
                    return true;

                case vk::Format::eBc7UnormBlock:
                case vk::Format::eBc7SrgbBlock:
                    rgba(4);
                    bcn::DecodeBc7(linear.data(), image.pixels.data(), width, height);
                    return true;

                case vk::Format::eBc4UnormBlock:
                case vk::Format::eBc4SnormBlock: {
                    std::vector<u8> decoded(std::max<size_t>(paddedPixels, size_t{width} * height), 0);
                    bool isSigned{format.vkFormat == vk::Format::eBc4SnormBlock};
                    bcn::DecodeBc4(linear.data(), decoded.data(), width, height, isSigned);
                    image.pixels.resize(size_t{width} * height * 4);
                    for (size_t i{}; i < size_t{width} * height; i++) {
                        u8 value{isSigned ? static_cast<u8>(std::max<i32>(static_cast<i8>(decoded[i]), 0) * 2) : decoded[i]};
                        std::memset(&image.pixels[i * 4], value, 3);
                        image.pixels[i * 4 + 3] = 255;
                    }
                    return true;
                }

                case vk::Format::eBc5UnormBlock:
                case vk::Format::eBc5SnormBlock: {
                    std::vector<u8> decoded(std::max<size_t>(paddedPixels * 2, size_t{width} * height * 2), 0);
                    bool isSigned{format.vkFormat == vk::Format::eBc5SnormBlock};
                    bcn::DecodeBc5(linear.data(), decoded.data(), width, height, isSigned);
                    image.pixels.resize(size_t{width} * height * 4);
                    auto convert{[isSigned](u8 value) -> u8 { return isSigned ? static_cast<u8>(std::max<i32>(static_cast<i8>(value), 0) * 2) : value; }};
                    for (size_t i{}; i < size_t{width} * height; i++) {
                        image.pixels[i * 4 + 0] = convert(decoded[i * 2]);
                        image.pixels[i * 4 + 1] = convert(decoded[i * 2 + 1]);
                        image.pixels[i * 4 + 2] = 0;
                        image.pixels[i * 4 + 3] = 255;
                    }
                    return true;
                }

                case vk::Format::eBc6HUfloatBlock:
                case vk::Format::eBc6HSfloatBlock: {
                    std::vector<u8> decoded(std::max<size_t>(paddedPixels * 8, size_t{width} * height * 8), 0);
                    bcn::DecodeBc6(linear.data(), decoded.data(), width, height, format.vkFormat == vk::Format::eBc6HSfloatBlock);
                    image.pixels.resize(size_t{width} * height * 4);
                    for (size_t i{}; i < size_t{width} * height; i++) {
                        for (size_t c{}; c < 3; c++) {
                            u16 half;
                            std::memcpy(&half, &decoded[i * 8 + c * 2], 2);
                            image.pixels[i * 4 + c] = ToByte(HalfToFloat(half));
                        }
                        image.pixels[i * 4 + 3] = 255;
                    }
                    return true;
                }

                default:
                    return false;
            }
        }

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
in vec2 vUv;
out vec4 fragColor;
void main() {
    fragColor = vec4(texture(uTexture, vUv).rgb, 1.0);
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
        LOGINF("OpenGL ES: the texture gallery shows the textures of the game (decoded here), a program of GLSL ES 3.20 was compiled and linked");
    }

    GalleryView::~GalleryView() {
        for (auto &slot : slots)
            if (slot.texture)
                glDeleteTextures(1, &slot.texture);
        if (program)
            glDeleteProgram(program);
    }

    void GalleryView::Show(const GalleryEntry &entry) {
        DecodedImage image;
        if (!Decode(entry, image)) {
            LOGINF("OpenGL ES gallery: texture #{} {}x{} {} isn't handled by the gallery", entry.number, entry.guest.dimensions.width, entry.guest.dimensions.height, vk::to_string(entry.guest.format->vkFormat));
            return;
        }

        auto &slot{slots[nextSlot]};
        nextSlot = (nextSlot + 1) % slots.size();
        if (slot.texture)
            glDeleteTextures(1, &slot.texture);

        glGenTextures(1, &slot.texture);
        glBindTexture(GL_TEXTURE_2D, slot.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        if (image.compressedFormat) {
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, image.compressedFormat, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), 0, static_cast<GLsizei>(entry.guest.format->GetSize(image.width, image.height)), image.pixels.data());
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
        }

        if (auto error{glGetError()}; error != GL_NO_ERROR)
            throw exception("Uploading the texture #{} ({}x{} {}) to OpenGL ES failed: error 0x{:X}", entry.number, image.width, image.height, image.description, error);

        slot.width = image.width;
        slot.height = image.height;
        LOGINF("OpenGL ES gallery: texture #{} {}x{} {}, tiling: {}", entry.number, image.width, image.height, image.description, entry.guest.tileConfig.mode == texture::TileMode::Block ? "block linear" : entry.guest.tileConfig.mode == texture::TileMode::Pitch ? "pitch" : "linear");
    }

    void GalleryView::Draw(i32 width, i32 height, u64 frame) {
        constexpr size_t DecodesPerFrame{2};

        GalleryEntry entry;
        for (size_t i{}; i < DecodesPerFrame && TextureGallery::Get().TakePending(entry); i++)
            Show(entry);

        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(program);
        glUniform1i(textureLocation, 0);
        glActiveTexture(GL_TEXTURE0);

        constexpr float Padding{0.02f}; // In normalized device coordinates, so it is a fraction of the cell
        float cellWidth{2.0f / Columns}, cellHeight{2.0f / Rows};
        for (size_t i{}; i < slots.size(); i++) {
            const auto &slot{slots[i]};
            if (!slot.texture || !slot.width || !slot.height)
                continue;

            float column{static_cast<float>(i % Columns)}, row{static_cast<float>(i / Columns)};
            float cellX{-1.0f + column * cellWidth}, cellY{1.0f - (row + 1.0f) * cellHeight};

            // The texture is scaled to fit in the cell without changing its aspect ratio (in pixels, not in normalized device coordinates)
            float cellPixelsWidth{static_cast<float>(width) / Columns * (1.0f - Padding * 2)}, cellPixelsHeight{static_cast<float>(height) / Rows * (1.0f - Padding * 2)};
            float scale{std::min(cellPixelsWidth / static_cast<float>(slot.width), cellPixelsHeight / static_cast<float>(slot.height))};
            float drawWidth{static_cast<float>(slot.width) * scale / static_cast<float>(width) * 2.0f}, drawHeight{static_cast<float>(slot.height) * scale / static_cast<float>(height) * 2.0f};
            float x0{cellX + (cellWidth - drawWidth) / 2.0f}, y0{cellY + (cellHeight - drawHeight) / 2.0f};

            glBindTexture(GL_TEXTURE_2D, slot.texture);
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
