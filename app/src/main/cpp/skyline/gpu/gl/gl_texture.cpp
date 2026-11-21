// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <cstring>
#include <GLES3/gl32.h>
#include <common.h>
#include <gpu/texture/layout.h>
#include <gpu/texture/bc_decoder.h>
#include "gl_texture.h"

namespace skyline::gpu::gl {
    namespace {
        u32 MaxMipLevels(u32 width, u32 height, u32 depth) {
            u32 size{std::max({width, height, depth, 1U})}, levels{1};
            while (size > 1) {
                size >>= 1;
                levels++;
            }
            return levels;
        }

        /**
         * @brief Converts a level in the linear layout of the guest format to the format that is uploaded
         * @return The data for the upload, it's 'linear' itself when no conversion is needed
         */
        const std::vector<u8> &Convert(const GlFormat &format, const texture::Dimensions &dimensions, const std::vector<u8> &linear, std::vector<u8> &converted) {
            if (format.conversion == Conversion::None)
                return linear;

            if (format.conversion == Conversion::SwapRedBlue) {
                converted = linear;
                for (size_t i{}; i + 3 < converted.size(); i += 4)
                    std::swap(converted[i], converted[i + 2]);
                return converted;
            }

            // The block decoders write whole blocks, so the output is padded to a multiple of the block size and then cut to the size of the level
            u32 width{dimensions.width}, height{dimensions.height};
            size_t paddedPixels{static_cast<size_t>((width + 3) & ~3U) * ((height + 3) & ~3U)};
            std::vector<u8> padded(paddedPixels * format.hostBytesPerPixel, 0);

            switch (format.conversion) {
                case Conversion::Bc1:
                    bcn::DecodeBc1(linear.data(), padded.data(), width, height, true);
                    break;
                case Conversion::Bc2:
                    bcn::DecodeBc2(linear.data(), padded.data(), width, height);
                    break;
                case Conversion::Bc3:
                    bcn::DecodeBc3(linear.data(), padded.data(), width, height);
                    break;
                case Conversion::Bc4:
                case Conversion::Bc4Signed:
                    bcn::DecodeBc4(linear.data(), padded.data(), width, height, format.conversion == Conversion::Bc4Signed);
                    break;
                case Conversion::Bc5:
                case Conversion::Bc5Signed:
                    bcn::DecodeBc5(linear.data(), padded.data(), width, height, format.conversion == Conversion::Bc5Signed);
                    break;
                case Conversion::Bc6:
                case Conversion::Bc6Signed:
                    bcn::DecodeBc6(linear.data(), padded.data(), width, height, format.conversion == Conversion::Bc6Signed);
                    break;
                case Conversion::Bc7:
                    bcn::DecodeBc7(linear.data(), padded.data(), width, height);
                    break;
                default:
                    throw exception("Unhandled texture conversion: {}", static_cast<u32>(format.conversion));
            }

            padded.resize(static_cast<size_t>(width) * height * format.hostBytesPerPixel);
            converted = std::move(padded);
            return converted;
        }
    }

    GlTexture::GlTexture(const GuestTexture &pGuest) : guest{pGuest}, glFormat{GetGlFormat(pGuest.format->vkFormat)} {
        if (!glFormat)
            throw exception("The OpenGL ES backend doesn't handle the texture format {} yet", vk::to_string(guest.format->vkFormat));

        const auto &format{*guest.format};
        layers = std::max(guest.layerCount, 1U);
        switch (guest.viewType) {
            case vk::ImageViewType::e2D:
                target = GL_TEXTURE_2D;
                layers = 1;
                break;
            case vk::ImageViewType::e2DArray:
                target = layers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
                if (target == GL_TEXTURE_2D)
                    layers = 1;
                break;
            case vk::ImageViewType::eCube:
                target = GL_TEXTURE_CUBE_MAP;
                layers = 6;
                break;
            case vk::ImageViewType::e3D:
                target = GL_TEXTURE_3D;
                layers = 1;
                break;
            default:
                throw exception("The OpenGL ES backend doesn't handle the texture type {} yet", vk::to_string(guest.viewType));
        }

        u32 depth{target == GL_TEXTURE_3D ? std::max(guest.dimensions.depth, 1U) : 1U};
        levels = std::clamp(guest.mipLevelCount, 1U, MaxMipLevels(guest.dimensions.width, guest.dimensions.height, depth));

        mipLayouts = texture::GetBlockLinearMipLayout(
            guest.dimensions,
            format.blockHeight, format.blockWidth, format.bpb,
            format.blockHeight, format.blockWidth, format.bpb, // The target format isn't used here, the conversion is done after the layout was taken out
            guest.tileConfig.blockHeight, guest.tileConfig.blockDepth,
            levels
        );

        glGenTextures(1, &name);
        glBindTexture(target, name);
        if (target == GL_TEXTURE_2D)
            glTexStorage2D(target, static_cast<GLsizei>(levels), glFormat->internalFormat, static_cast<GLsizei>(guest.dimensions.width), static_cast<GLsizei>(guest.dimensions.height));
        else if (target == GL_TEXTURE_CUBE_MAP)
            glTexStorage2D(target, static_cast<GLsizei>(levels), glFormat->internalFormat, static_cast<GLsizei>(guest.dimensions.width), static_cast<GLsizei>(guest.dimensions.height));
        else if (target == GL_TEXTURE_2D_ARRAY)
            glTexStorage3D(target, static_cast<GLsizei>(levels), glFormat->internalFormat, static_cast<GLsizei>(guest.dimensions.width), static_cast<GLsizei>(guest.dimensions.height), static_cast<GLsizei>(layers));
        else
            glTexStorage3D(target, static_cast<GLsizei>(levels), glFormat->internalFormat, static_cast<GLsizei>(guest.dimensions.width), static_cast<GLsizei>(guest.dimensions.height), static_cast<GLsizei>(depth));

        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (glFormat->isInteger) {
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST); // Integer textures can't be filtered
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }

        if (glFormat->swizzle == Swizzle::Gray) {
            glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, GL_RED);
            glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, GL_RED);
            glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        } else if (glFormat->swizzle == Swizzle::RedGreen) {
            glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, GL_ZERO);
            glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        }

        if (auto error{glGetError()}; error != GL_NO_ERROR) {
            glDeleteTextures(1, &name);
            throw exception("Creating the OpenGL ES texture ({}x{} {}, {} level(s), {} layer(s)) failed: error 0x{:X}", guest.dimensions.width, guest.dimensions.height, vk::to_string(format.vkFormat), levels, layers, error);
        }
    }

    GlTexture::~GlTexture() {
        if (name)
            glDeleteTextures(1, &name);
    }

    size_t GlTexture::GuestSize() {
        return static_cast<size_t>(guest.GetLayerStride()) * (target == GL_TEXTURE_3D ? 1 : layers);
    }

    void GlTexture::UploadLevel(u32 layer, u32 level, const texture::MipLevelLayout &layout, const std::vector<u8> &linear) {
        std::vector<u8> converted;
        const auto &data{Convert(*glFormat, layout.dimensions, linear, converted)};

        auto width{static_cast<GLsizei>(layout.dimensions.width)}, height{static_cast<GLsizei>(layout.dimensions.height)};
        auto glLevel{static_cast<GLint>(level)};
        if (target == GL_TEXTURE_2D || target == GL_TEXTURE_CUBE_MAP) {
            u32 uploadTarget{target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer : target};
            if (glFormat->compressed)
                glCompressedTexSubImage2D(uploadTarget, glLevel, 0, 0, width, height, glFormat->internalFormat, static_cast<GLsizei>(data.size()), data.data());
            else
                glTexSubImage2D(uploadTarget, glLevel, 0, 0, width, height, glFormat->format, glFormat->type, data.data());
        } else if (target == GL_TEXTURE_2D_ARRAY) {
            if (glFormat->compressed)
                glCompressedTexSubImage3D(target, glLevel, 0, 0, static_cast<GLint>(layer), width, height, 1, glFormat->internalFormat, static_cast<GLsizei>(data.size()), data.data());
            else
                glTexSubImage3D(target, glLevel, 0, 0, static_cast<GLint>(layer), width, height, 1, glFormat->format, glFormat->type, data.data());
        } else {
            auto depth{static_cast<GLsizei>(std::max(layout.dimensions.depth, 1U))};
            if (glFormat->compressed)
                glCompressedTexSubImage3D(target, glLevel, 0, 0, 0, width, height, depth, glFormat->internalFormat, static_cast<GLsizei>(data.size()), data.data());
            else
                glTexSubImage3D(target, glLevel, 0, 0, 0, width, height, depth, glFormat->format, glFormat->type, data.data());
        }
    }

    void GlTexture::Upload(const u8 *guestData) {
        glBindTexture(target, name);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        const auto &format{*guest.format};
        auto layerStride{guest.GetLayerStride()};
        std::vector<u8> linear;

        for (u32 layer{}; layer < layers; layer++) {
            const u8 *input{guestData + static_cast<size_t>(layer) * layerStride};

            if (guest.tileConfig.mode == texture::TileMode::Block) {
                // The guest has all the mip levels of a layer one after the other
                const u8 *inputLevel{input};
                for (u32 level{}; level < levels; level++) {
                    const auto &layout{mipLayouts[level]};
                    linear.assign(layout.linearSize, 0);
                    texture::CopyBlockLinearToLinear(
                        layout.dimensions,
                        format.blockWidth, format.blockHeight, format.bpb,
                        layout.blockHeight, layout.blockDepth,
                        const_cast<u8 *>(inputLevel), linear.data()
                    );
                    UploadLevel(layer, level, layout, linear);
                    inputLevel += layout.blockLinearSize;
                }
            } else {
                // Pitch and linear textures only have one level
                const auto &layout{mipLayouts.front()};
                linear.assign(layout.linearSize, 0);
                if (guest.tileConfig.mode == texture::TileMode::Pitch) {
                    GuestTexture level0{guest};
                    level0.dimensions.depth = 1;
                    texture::CopyPitchLinearToLinear(level0, const_cast<u8 *>(input), linear.data());
                } else {
                    std::memcpy(linear.data(), input, linear.size());
                }
                UploadLevel(layer, 0, layout, linear);
            }
        }

        if (auto error{glGetError()}; error != GL_NO_ERROR)
            throw exception("Uploading the OpenGL ES texture ({}x{} {}) failed: error 0x{:X}", guest.dimensions.width, guest.dimensions.height, vk::to_string(format.vkFormat), error);
    }
}
