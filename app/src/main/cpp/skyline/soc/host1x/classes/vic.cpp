// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)
// The conversion of the frames follows the VIC of Eden (video_core/host1x/vic.cpp), see the notice in video/eden

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <gpu/texture/layout.h>
#include "eden/video_core/host1x/host1x.h"
#include "vic.h"

namespace skyline::soc::host1x {
    using namespace Tegra::Host1x;

    VicClass::VicClass(const DeviceState &state, std::function<void()> opDoneCallback)
        : state(state), opDoneCallback(std::move(opDoneCallback)) {}

    void VicClass::CallMethod(u32 method, u32 argument) {
        // The methods are numbered in units of 4 bytes
        constexpr u32 ExecuteMethod{offsetof(VicRegisters, execute) / sizeof(u32)};

        if (method >= regs.reg_array.size()) {
            LOGW("VIC method outside of the registers: 0x{:X} (argument 0x{:X})", method, argument);
            return;
        }

        regs.reg_array[method] = argument;

        if (method == ExecuteMethod) {
            // A frame that can't be composed is skipped, the game goes on and the syncpoint of the command is still signalled
            try {
                Execute();
            } catch (const std::exception &e) {
                LOGE("Failed to compose a video frame: {}", e.what());
            }
        }
    }

    void VicClass::Execute() {
        auto &host1x{Host1x::Get(state)};

        ConfigStruct config{};
        host1x.gmmu_manager.ReadBlock(regs.config_struct_offset.Address(), &config, sizeof(ConfigStruct));

        u32 outputWidth{config.output_surface_config.out_surface_width + 1};
        u32 outputHeight{config.output_surface_config.out_surface_height + 1};
        outputSurface.resize(static_cast<size_t>(outputWidth) * outputHeight);

        bool decodedFrame{};
        for (size_t i{}; i < config.slot_structs.size(); i++) {
            const auto &slot{config.slot_structs[i]};
            if (!slot.config.slot_enable)
                continue;

            auto frame{host1x.frame_queue.GetFrame(0, regs.surfaces[i][SurfaceIndex::Current].luma.Address())};
            if (!frame)
                continue;

            ReadSlot(slot, *frame);
            Blend(config, slot);
            decodedFrame = true;
        }

        if (decodedFrame)
            hasDecodedFrame = true;
        else if (!hasDecodedFrame)
            return; // Nothing was ever decoded, the game's surface is left as it is instead of being filled with black

        auto format{config.output_surface_config.out_pixel_format.Value()};
        switch (format) {
            case VideoPixelFormat::A8B8G8R8:
            case VideoPixelFormat::X8B8G8R8:
            case VideoPixelFormat::A8R8G8B8:
                WriteRgb(config.output_surface_config, format);
                break;
            case VideoPixelFormat::Y8__V8U8_N420:
                WriteYuv420(config.output_surface_config);
                break;
            default:
                if (!unsupportedFormatReported) {
                    unsupportedFormatReported = true;
                    LOGE("The VIC output pixel format isn't supported: {}", static_cast<u32>(format));
                }
                break;
        }
    }

    void VicClass::ReadSlot(const SlotStruct &slot, const FFmpeg::Frame &frame) {
        if (slot.config.frame_format != DxvhadFrameFormat::Progressive)
            LOGD("The frame is interlaced, it is composed as if it was progressive");

        const bool planar{frame.GetPixelFormat() == FFmpeg::PixelFormat::Yuv420P};
        u32 surfaceWidth{slot.surface_config.slot_surface_width + 1};
        u32 surfaceHeight{slot.surface_config.slot_surface_height + 1};

        u32 width{std::min<u32>(static_cast<u32>(frame.GetWidth()), surfaceWidth)};
        u32 height{std::min<u32>(static_cast<u32>(frame.GetHeight()), surfaceHeight)};

        slotSurface.assign(static_cast<size_t>(surfaceWidth) * surfaceHeight, Pixel{});

        const u8 *luma{frame.GetPlane(0)};
        const u8 *chromaU{frame.GetPlane(1)};
        const u8 *chromaV{planar ? frame.GetPlane(2) : nullptr};
        size_t lumaStride{static_cast<size_t>(frame.GetStride(0))};
        size_t chromaStride{static_cast<size_t>(frame.GetStride(1))};
        u16 alpha{static_cast<u16>(slot.config.planar_alpha.Value())};

        for (size_t y{}; y < height; y++) {
            size_t lumaRow{y * lumaStride}, chromaRow{(y / 2) * chromaStride}, destination{y * surfaceWidth};
            for (size_t x{}; x < width; x++) {
                Pixel &pixel{slotSurface[destination + x]};
                pixel.r = static_cast<u16>(luma[lumaRow + x] << 2);
                if (planar) {
                    pixel.g = static_cast<u16>(chromaU[chromaRow + x / 2] << 2);
                    pixel.b = static_cast<u16>(chromaV[chromaRow + x / 2] << 2);
                } else {
                    pixel.g = static_cast<u16>(chromaU[chromaRow + (x & ~size_t{1}) + 0] << 2);
                    pixel.b = static_cast<u16>(chromaU[chromaRow + (x & ~size_t{1}) + 1] << 2);
                }
                pixel.a = alpha;
            }
        }
    }

    void VicClass::Blend(const ConfigStruct &config, const SlotStruct &slot) {
        auto addOne{[](u32 value) -> u32 { return value + (value > 0 ? 1 : 0); }};

        u32 sourceLeft{addOne(static_cast<u32>(slot.config.source_rect_left.Value()))};
        u32 sourceRight{addOne(static_cast<u32>(slot.config.source_rect_right.Value()))};
        u32 sourceTop{addOne(static_cast<u32>(slot.config.source_rect_top.Value()))};
        u32 sourceBottom{addOne(static_cast<u32>(slot.config.source_rect_bottom.Value()))};

        u32 destinationLeft{addOne(static_cast<u32>(slot.config.dest_rect_left.Value()))};
        u32 destinationRight{addOne(static_cast<u32>(slot.config.dest_rect_right.Value()))};
        u32 destinationTop{addOne(static_cast<u32>(slot.config.dest_rect_top.Value()))};
        u32 destinationBottom{addOne(static_cast<u32>(slot.config.dest_rect_bottom.Value()))};

        u32 rectLeft{addOne(static_cast<u32>(config.output_config.target_rect_left.Value()))};
        u32 rectRight{addOne(static_cast<u32>(config.output_config.target_rect_right.Value()))};
        u32 rectTop{addOne(static_cast<u32>(config.output_config.target_rect_top.Value()))};
        u32 rectBottom{addOne(static_cast<u32>(config.output_config.target_rect_bottom.Value()))};

        rectLeft = std::max(rectLeft, destinationLeft);
        rectRight = std::min(rectRight, destinationRight);
        rectTop = std::max(rectTop, destinationTop);
        rectBottom = std::min(rectBottom, destinationBottom);

        sourceLeft = std::max(sourceLeft, rectLeft);
        sourceRight = std::min(sourceRight, rectRight);
        sourceTop = std::max(sourceTop, rectTop);
        sourceBottom = std::min(sourceBottom, rectBottom);

        u32 outputWidth{config.output_surface_config.out_surface_width + 1};
        u32 outputHeight{config.output_surface_config.out_surface_height + 1};
        u32 inputWidth{slot.surface_config.slot_surface_width + 1};
        u32 inputHeight{slot.surface_config.slot_surface_height + 1};

        sourceBottom = std::min({sourceBottom, outputHeight, inputHeight});
        sourceRight = std::min({sourceRight, outputWidth, inputWidth});
        if (sourceRight <= sourceLeft || sourceBottom <= sourceTop)
            return;

        if (slot.color_matrix.matrix_enable) {
            // The matrix turns the YUV values of a pixel into RGB: three rows of three coefficients and an offset, with a shift for the products
            const s32 c00{static_cast<s32>(slot.color_matrix.matrix_coeff00.Value())}, c01{static_cast<s32>(slot.color_matrix.matrix_coeff01.Value())}, c02{static_cast<s32>(slot.color_matrix.matrix_coeff02.Value())}, c03{static_cast<s32>(slot.color_matrix.matrix_coeff03.Value())};
            const s32 c10{static_cast<s32>(slot.color_matrix.matrix_coeff10.Value())}, c11{static_cast<s32>(slot.color_matrix.matrix_coeff11.Value())}, c12{static_cast<s32>(slot.color_matrix.matrix_coeff12.Value())}, c13{static_cast<s32>(slot.color_matrix.matrix_coeff13.Value())};
            const s32 c20{static_cast<s32>(slot.color_matrix.matrix_coeff20.Value())}, c21{static_cast<s32>(slot.color_matrix.matrix_coeff21.Value())}, c22{static_cast<s32>(slot.color_matrix.matrix_coeff22.Value())}, c23{static_cast<s32>(slot.color_matrix.matrix_coeff23.Value())};
            const s32 shift{static_cast<s32>(slot.color_matrix.matrix_r_shift.Value())};
            const s32 clampMin{static_cast<s32>(slot.config.soft_clamp_low.Value())};
            const s32 clampMax{static_cast<s32>(slot.config.soft_clamp_high.Value())};

            for (u32 y{sourceTop}; y < sourceBottom; y++) {
                size_t source{static_cast<size_t>(y) * inputWidth};
                size_t destination{static_cast<size_t>(y) * outputWidth + (rectLeft - sourceLeft)};
                for (u32 x{sourceLeft}; x < sourceRight; x++) {
                    const Pixel &in{slotSurface[source + x]};
                    s32 r{static_cast<s32>(in.r) * c00 + static_cast<s32>(in.g) * c01 + static_cast<s32>(in.b) * c02};
                    s32 g{static_cast<s32>(in.r) * c10 + static_cast<s32>(in.g) * c11 + static_cast<s32>(in.b) * c12};
                    s32 b{static_cast<s32>(in.r) * c20 + static_cast<s32>(in.g) * c21 + static_cast<s32>(in.b) * c22};
                    r = ((r >> shift) + c03) >> 8;
                    g = ((g >> shift) + c13) >> 8;
                    b = ((b >> shift) + c23) >> 8;

                    if (destination + x >= outputSurface.size())
                        return;
                    outputSurface[destination + x] = {
                        static_cast<u16>(std::clamp(r, clampMin, clampMax)),
                        static_cast<u16>(std::clamp(g, clampMin, clampMax)),
                        static_cast<u16>(std::clamp(b, clampMin, clampMax)),
                        static_cast<u16>(std::clamp(static_cast<s32>(in.a), clampMin, clampMax)),
                    };
                }
            }
        } else {
            u32 copyWidth{std::min(sourceRight - sourceLeft, rectRight - rectLeft)};
            for (u32 y{sourceTop}; y < sourceBottom; y++) {
                size_t source{static_cast<size_t>(y) * inputWidth + sourceLeft};
                size_t destination{static_cast<size_t>(y) * outputWidth + rectLeft};
                if (destination + copyWidth <= outputSurface.size())
                    std::memcpy(&outputSurface[destination], &slotSurface[source], copyWidth * sizeof(Pixel));
            }
        }
    }

    void VicClass::WriteRgb(const OutputSurfaceConfig &outputConfig, VideoPixelFormat format) {
        constexpr u32 BytesPerPixel{4};

        u32 surfaceWidth{outputConfig.out_surface_width + 1};
        u32 surfaceHeight{outputConfig.out_surface_height + 1};
        u32 lumaWidth{outputConfig.out_luma_width + 1};
        u32 lumaHeight{outputConfig.out_luma_height + 1};
        u32 pitch{(lumaWidth * BytesPerPixel + 0xF) & ~u32{0xF}};
        bool blockLinear{outputConfig.out_block_kind == BlkKind::Generic_16Bx2};
        if (blockLinear)
            pitch = lumaWidth * BytesPerPixel; // The block linear copy reads rows without padding

        std::vector<u8> linear(static_cast<size_t>(pitch) * lumaHeight, 0);

        // The surface is made of 10 bit components, the game gets 8 bit ones in the byte order its format asks for
        u32 width{std::min(surfaceWidth, lumaWidth)};
        u32 height{std::min(surfaceHeight, lumaHeight)};
        for (u32 y{}; y < height; y++) {
            const Pixel *in{&outputSurface[static_cast<size_t>(y) * surfaceWidth]};
            u8 *out{&linear[static_cast<size_t>(y) * pitch]};
            for (u32 x{}; x < width; x++, out += 4) {
                if (format == VideoPixelFormat::A8R8G8B8) {
                    out[0] = static_cast<u8>(in[x].b >> 2);
                    out[1] = static_cast<u8>(in[x].g >> 2);
                    out[2] = static_cast<u8>(in[x].r >> 2);
                    out[3] = static_cast<u8>(in[x].a >> 2);
                } else {
                    out[0] = static_cast<u8>(in[x].r >> 2);
                    out[1] = static_cast<u8>(in[x].g >> 2);
                    out[2] = static_cast<u8>(in[x].b >> 2);
                    out[3] = static_cast<u8>(in[x].a >> 2);
                }
            }
        }

        auto &host1x{Host1x::Get(state)};
        u32 address{regs.output_surface.luma.Address()};
        if (blockLinear) {
            // The block height is a power of two number of GOBs, the value in the configuration is its logarithm
            size_t gobBlockHeight{size_t{1} << outputConfig.out_block_height.Value()};
            gpu::texture::Dimensions dimensions{lumaWidth, lumaHeight, 1};
            size_t size{gpu::texture::GetBlockLinearLayerSize(dimensions, 1, 1, BytesPerPixel, gobBlockHeight, 1)};
            std::vector<u8> swizzled(size, 0);
            gpu::texture::CopyLinearToBlockLinear(dimensions, 1, 1, BytesPerPixel, gobBlockHeight, 1, linear.data(), swizzled.data());
            host1x.gmmu_manager.WriteBlock(address, swizzled.data(), swizzled.size());
        } else {
            host1x.gmmu_manager.WriteBlock(address, linear.data(), linear.size());
        }
    }

    void VicClass::WriteYuv420(const OutputSurfaceConfig &outputConfig) {
        u32 surfaceWidth{outputConfig.out_surface_width + 1};
        u32 surfaceHeight{outputConfig.out_surface_height + 1};
        u32 lumaWidth{outputConfig.out_luma_width + 1};
        u32 lumaHeight{outputConfig.out_luma_height + 1};
        u32 chromaWidth{outputConfig.out_chroma_width + 1};
        u32 chromaHeight{outputConfig.out_chroma_height + 1};
        bool blockLinear{outputConfig.out_block_kind == BlkKind::Generic_16Bx2};

        // A chroma pair (2 bytes) covers 2x2 pixels, so the chroma plane has half the width and the height of the luma one but 2 bytes per element
        auto align{[](u32 value) { return (value + 0xF) & ~u32{0xF}; }};
        u32 lumaPitch{blockLinear ? lumaWidth : align(lumaWidth)};
        u32 chromaPitch{blockLinear ? chromaWidth * 2 : align(chromaWidth * 2)};

        std::vector<u8> luma(static_cast<size_t>(lumaPitch) * lumaHeight, 0);
        std::vector<u8> chroma(static_cast<size_t>(chromaPitch) * chromaHeight, 0);

        // The surface is made of 10 bit components, in a YUV output the red one is the luma and the green and blue ones are the two chroma values. The chroma of the first pixel of every pair of pixels in a row and of every other row is the one that is kept
        u32 width{std::min(surfaceWidth, lumaWidth)};
        u32 height{std::min(surfaceHeight, lumaHeight)};
        for (u32 y{}; y < height; y++) {
            const Pixel *in{&outputSurface[static_cast<size_t>(y) * surfaceWidth]};
            u8 *lumaRow{&luma[static_cast<size_t>(y) * lumaPitch]};
            for (u32 x{}; x < width; x++)
                lumaRow[x] = static_cast<u8>(in[x].r >> 2);

            u32 chromaRowIndex{y / 2};
            if (y % 2 == 0 && chromaRowIndex < chromaHeight) {
                u8 *chromaRow{&chroma[static_cast<size_t>(chromaRowIndex) * chromaPitch]};
                for (u32 x{}; x + 1 < chromaPitch && x < width; x += 2) {
                    chromaRow[x + 0] = static_cast<u8>(in[x].g >> 2);
                    chromaRow[x + 1] = static_cast<u8>(in[x].b >> 2);
                }
            }
        }

        auto &host1x{Host1x::Get(state)};
        u32 lumaAddress{regs.output_surface.luma.Address()};
        u32 chromaAddress{regs.output_surface.chroma_u.Address()};

        if (blockLinear) {
            // The block height is a power of two number of GOBs, the value in the configuration is its logarithm
            size_t gobBlockHeight{size_t{1} << outputConfig.out_block_height.Value()};

            gpu::texture::Dimensions lumaDimensions{lumaWidth, lumaHeight, 1};
            size_t lumaSize{gpu::texture::GetBlockLinearLayerSize(lumaDimensions, 1, 1, 1, gobBlockHeight, 1)};
            std::vector<u8> lumaSwizzled(lumaSize, 0);
            gpu::texture::CopyLinearToBlockLinear(lumaDimensions, 1, 1, 1, gobBlockHeight, 1, luma.data(), lumaSwizzled.data());
            host1x.gmmu_manager.WriteBlock(lumaAddress, lumaSwizzled.data(), lumaSwizzled.size());

            gpu::texture::Dimensions chromaDimensions{chromaWidth, chromaHeight, 1};
            size_t chromaSize{gpu::texture::GetBlockLinearLayerSize(chromaDimensions, 1, 1, 2, gobBlockHeight, 1)};
            std::vector<u8> chromaSwizzled(chromaSize, 0);
            gpu::texture::CopyLinearToBlockLinear(chromaDimensions, 1, 1, 2, gobBlockHeight, 1, chroma.data(), chromaSwizzled.data());
            host1x.gmmu_manager.WriteBlock(chromaAddress, chromaSwizzled.data(), chromaSwizzled.size());
        } else {
            // The chroma address of some games is too close to the luma one, so that the luma (with its padding) runs into the start of the chroma: the luma is written first and the chroma after it
            host1x.gmmu_manager.WriteBlock(lumaAddress, luma.data(), luma.size());
            host1x.gmmu_manager.WriteBlock(chromaAddress, chroma.data(), chroma.size());
        }
    }
}
