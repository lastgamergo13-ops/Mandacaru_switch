// SPDX-License-Identifier: MPL-2.0

#include <array>
#include <GLES3/gl32.h>
#include "gl_format.h"

namespace skyline::gpu::gl {
    namespace {
        constexpr u32 AstcBase{0x93B0}; // GL_COMPRESSED_RGBA_ASTC_4x4_KHR, the block sizes follow in this order: 4x4 5x4 5x5 6x5 6x6 8x5 8x6 8x8 10x5 10x6 10x8 10x10 12x10 12x12
        constexpr u32 AstcSrgbBase{0x93D0}; // GL_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR

        constexpr GlFormat Plain(u32 internalFormat, u32 format, u32 type, u32 bytesPerPixel, bool isInteger = false, bool isSrgb = false, Swizzle swizzle = Swizzle::None, Conversion conversion = Conversion::None) {
            return GlFormat{internalFormat, format, type, false, isInteger, isSrgb, conversion, swizzle, bytesPerPixel};
        }

        constexpr GlFormat Astc(u32 index, bool srgb) {
            return GlFormat{(srgb ? AstcSrgbBase : AstcBase) + index, 0, 0, true, false, srgb, Conversion::None, Swizzle::None, 0};
        }

        // The formats that are handed out by GetGlFormat
        const GlFormat R8Unorm{Plain(GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, false, false, Swizzle::Gray)};
        const GlFormat R8Snorm{Plain(GL_R8_SNORM, GL_RED, GL_BYTE, 1, false, false, Swizzle::Gray)};
        const GlFormat R8Uint{Plain(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE, 1, true)};
        const GlFormat R8Sint{Plain(GL_R8I, GL_RED_INTEGER, GL_BYTE, 1, true)};
        const GlFormat Rg8Unorm{Plain(GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2, false, false, Swizzle::RedGreen)};
        const GlFormat Rg8Snorm{Plain(GL_RG8_SNORM, GL_RG, GL_BYTE, 2, false, false, Swizzle::RedGreen)};
        const GlFormat Rgba8Unorm{Plain(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4)};
        const GlFormat Rgba8Srgb{Plain(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, true)};
        const GlFormat Rgba8Snorm{Plain(GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, 4)};
        const GlFormat Rgba8Uint{Plain(GL_RGBA8UI, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, 4, true)};
        const GlFormat Rgba8Sint{Plain(GL_RGBA8I, GL_RGBA_INTEGER, GL_BYTE, 4, true)};
        const GlFormat Bgra8Unorm{Plain(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, false, Swizzle::None, Conversion::SwapRedBlue)};
        const GlFormat Bgra8Srgb{Plain(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, true, Swizzle::None, Conversion::SwapRedBlue)};
        const GlFormat R16Sfloat{Plain(GL_R16F, GL_RED, GL_HALF_FLOAT, 2, false, false, Swizzle::Gray)};
        const GlFormat Rg16Sfloat{Plain(GL_RG16F, GL_RG, GL_HALF_FLOAT, 4, false, false, Swizzle::RedGreen)};
        const GlFormat Rgba16Sfloat{Plain(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8)};
        const GlFormat R16Uint{Plain(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, 2, true)};
        const GlFormat R16Sint{Plain(GL_R16I, GL_RED_INTEGER, GL_SHORT, 2, true)};
        const GlFormat Rg16Uint{Plain(GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, 4, true)};
        const GlFormat Rgba16Uint{Plain(GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, 8, true)};
        const GlFormat R32Sfloat{Plain(GL_R32F, GL_RED, GL_FLOAT, 4, false, false, Swizzle::Gray)};
        const GlFormat Rg32Sfloat{Plain(GL_RG32F, GL_RG, GL_FLOAT, 8, false, false, Swizzle::RedGreen)};
        const GlFormat Rgba32Sfloat{Plain(GL_RGBA32F, GL_RGBA, GL_FLOAT, 16)};
        const GlFormat R32Uint{Plain(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 4, true)};
        const GlFormat Rgba32Uint{Plain(GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 16, true)};
        const GlFormat A2B10G10R10Unorm{Plain(GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 4)};
        const GlFormat B10G11R11Ufloat{Plain(GL_R11F_G11F_B10F, GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, 4)};

        const GlFormat Bc1Rgba{Plain(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, false, Swizzle::None, Conversion::Bc1)};
        const GlFormat Bc1RgbaSrgb{Plain(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, true, Swizzle::None, Conversion::Bc1)};
        const GlFormat Bc2{Plain(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, false, Swizzle::None, Conversion::Bc2)};
        const GlFormat Bc2Srgb{Plain(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, true, Swizzle::None, Conversion::Bc2)};
        const GlFormat Bc3{Plain(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, false, Swizzle::None, Conversion::Bc3)};
        const GlFormat Bc3Srgb{Plain(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, true, Swizzle::None, Conversion::Bc3)};
        const GlFormat Bc4{Plain(GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1, false, false, Swizzle::Gray, Conversion::Bc4)};
        const GlFormat Bc4Signed{Plain(GL_R8_SNORM, GL_RED, GL_BYTE, 1, false, false, Swizzle::Gray, Conversion::Bc4Signed)};
        const GlFormat Bc5{Plain(GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2, false, false, Swizzle::RedGreen, Conversion::Bc5)};
        const GlFormat Bc5Signed{Plain(GL_RG8_SNORM, GL_RG, GL_BYTE, 2, false, false, Swizzle::RedGreen, Conversion::Bc5Signed)};
        const GlFormat Bc6{Plain(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8, false, false, Swizzle::None, Conversion::Bc6)};
        const GlFormat Bc6Signed{Plain(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8, false, false, Swizzle::None, Conversion::Bc6Signed)};
        const GlFormat Bc7{Plain(GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, false, Swizzle::None, Conversion::Bc7)};
        const GlFormat Bc7Srgb{Plain(GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, false, true, Swizzle::None, Conversion::Bc7)};

        constexpr std::array<GlFormat, 14> AstcUnorm{{
            Astc(0, false), Astc(1, false), Astc(2, false), Astc(3, false), Astc(4, false), Astc(5, false), Astc(6, false),
            Astc(7, false), Astc(8, false), Astc(9, false), Astc(10, false), Astc(11, false), Astc(12, false), Astc(13, false),
        }};
        constexpr std::array<GlFormat, 14> AstcSrgb{{
            Astc(0, true), Astc(1, true), Astc(2, true), Astc(3, true), Astc(4, true), Astc(5, true), Astc(6, true),
            Astc(7, true), Astc(8, true), Astc(9, true), Astc(10, true), Astc(11, true), Astc(12, true), Astc(13, true),
        }};
    }

    const GlFormat *GetGlFormat(vk::Format format) {
        using F = vk::Format;
        switch (format) {
            case F::eR8Unorm: return &R8Unorm;
            case F::eR8Snorm: return &R8Snorm;
            case F::eR8Uint: return &R8Uint;
            case F::eR8Sint: return &R8Sint;
            case F::eR8G8Unorm: return &Rg8Unorm;
            case F::eR8G8Snorm: return &Rg8Snorm;
            case F::eR8G8B8A8Unorm: return &Rgba8Unorm;
            case F::eR8G8B8A8Srgb: return &Rgba8Srgb;
            case F::eR8G8B8A8Snorm: return &Rgba8Snorm;
            case F::eR8G8B8A8Uint: return &Rgba8Uint;
            case F::eR8G8B8A8Sint: return &Rgba8Sint;
            case F::eB8G8R8A8Unorm: return &Bgra8Unorm;
            case F::eB8G8R8A8Srgb: return &Bgra8Srgb;
            case F::eR16Sfloat: return &R16Sfloat;
            case F::eR16G16Sfloat: return &Rg16Sfloat;
            case F::eR16G16B16A16Sfloat: return &Rgba16Sfloat;
            case F::eR16Uint: return &R16Uint;
            case F::eR16Sint: return &R16Sint;
            case F::eR16G16Uint: return &Rg16Uint;
            case F::eR16G16B16A16Uint: return &Rgba16Uint;
            case F::eR32Sfloat: return &R32Sfloat;
            case F::eR32G32Sfloat: return &Rg32Sfloat;
            case F::eR32G32B32A32Sfloat: return &Rgba32Sfloat;
            case F::eR32Uint: return &R32Uint;
            case F::eR32G32B32A32Uint: return &Rgba32Uint;
            case F::eA2B10G10R10UnormPack32: return &A2B10G10R10Unorm;
            case F::eB10G11R11UfloatPack32: return &B10G11R11Ufloat;

            case F::eBc1RgbaUnormBlock: return &Bc1Rgba;
            case F::eBc1RgbaSrgbBlock: return &Bc1RgbaSrgb;
            case F::eBc2UnormBlock: return &Bc2;
            case F::eBc2SrgbBlock: return &Bc2Srgb;
            case F::eBc3UnormBlock: return &Bc3;
            case F::eBc3SrgbBlock: return &Bc3Srgb;
            case F::eBc4UnormBlock: return &Bc4;
            case F::eBc4SnormBlock: return &Bc4Signed;
            case F::eBc5UnormBlock: return &Bc5;
            case F::eBc5SnormBlock: return &Bc5Signed;
            case F::eBc6HUfloatBlock: return &Bc6;
            case F::eBc6HSfloatBlock: return &Bc6Signed;
            case F::eBc7UnormBlock: return &Bc7;
            case F::eBc7SrgbBlock: return &Bc7Srgb;

            case F::eAstc4x4UnormBlock: return &AstcUnorm[0];
            case F::eAstc5x4UnormBlock: return &AstcUnorm[1];
            case F::eAstc5x5UnormBlock: return &AstcUnorm[2];
            case F::eAstc6x5UnormBlock: return &AstcUnorm[3];
            case F::eAstc6x6UnormBlock: return &AstcUnorm[4];
            case F::eAstc8x5UnormBlock: return &AstcUnorm[5];
            case F::eAstc8x6UnormBlock: return &AstcUnorm[6];
            case F::eAstc8x8UnormBlock: return &AstcUnorm[7];
            case F::eAstc10x5UnormBlock: return &AstcUnorm[8];
            case F::eAstc10x6UnormBlock: return &AstcUnorm[9];
            case F::eAstc10x8UnormBlock: return &AstcUnorm[10];
            case F::eAstc10x10UnormBlock: return &AstcUnorm[11];
            case F::eAstc12x10UnormBlock: return &AstcUnorm[12];
            case F::eAstc12x12UnormBlock: return &AstcUnorm[13];
            case F::eAstc4x4SrgbBlock: return &AstcSrgb[0];
            case F::eAstc5x4SrgbBlock: return &AstcSrgb[1];
            case F::eAstc5x5SrgbBlock: return &AstcSrgb[2];
            case F::eAstc6x5SrgbBlock: return &AstcSrgb[3];
            case F::eAstc6x6SrgbBlock: return &AstcSrgb[4];
            case F::eAstc8x5SrgbBlock: return &AstcSrgb[5];
            case F::eAstc8x6SrgbBlock: return &AstcSrgb[6];
            case F::eAstc8x8SrgbBlock: return &AstcSrgb[7];
            case F::eAstc10x5SrgbBlock: return &AstcSrgb[8];
            case F::eAstc10x6SrgbBlock: return &AstcSrgb[9];
            case F::eAstc10x8SrgbBlock: return &AstcSrgb[10];
            case F::eAstc10x10SrgbBlock: return &AstcSrgb[11];
            case F::eAstc12x10SrgbBlock: return &AstcSrgb[12];
            case F::eAstc12x12SrgbBlock: return &AstcSrgb[13];

            default:
                return nullptr;
        }
    }
}
