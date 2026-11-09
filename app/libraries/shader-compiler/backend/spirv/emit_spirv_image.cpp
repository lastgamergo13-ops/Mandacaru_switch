// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/static_vector.hpp>

#include <shader_compiler/backend/spirv/emit_spirv.h>
#include <shader_compiler/backend/spirv/emit_spirv_instructions.h>
#include <shader_compiler/backend/spirv/spirv_emit_context.h>
#include <shader_compiler/frontend/ir/modifiers.h>
#include <shader_compiler/common/log.h>

namespace Shader::Backend::SPIRV {
namespace {

enum class NonUniformKind {
    SampledImage,
    StorageImage,
    UniformTexelBuffer,
    StorageTexelBuffer,
};

[[nodiscard]] bool IsNonUniformSupported(const Profile& profile, NonUniformKind kind) noexcept {
    switch (kind) {
    case NonUniformKind::SampledImage:
        return profile.support_sampled_image_array_nonuniform_indexing;
    case NonUniformKind::StorageImage:
        return profile.support_storage_image_array_nonuniform_indexing;
    case NonUniformKind::UniformTexelBuffer:
        return profile.support_uniform_texel_buffer_array_nonuniform_indexing;
    case NonUniformKind::StorageTexelBuffer:
        return profile.support_storage_texel_buffer_array_nonuniform_indexing;
    }
    return false;
}

void DecorateNonUniform(EmitContext& ctx, Id object) {
    if (ctx.non_uniform_ids.contains(object.value)) {
        return;
    }
    ctx.Decorate(object, spv::Decoration::NonUniform);
    ctx.non_uniform_ids.insert(object.value);
}

/// Decorates the index of an array of descriptors as NonUniform when the host supports it
/// @return true if the index was decorated, the objects that come from it have to be as well
[[nodiscard]] bool MarkNonUniform(EmitContext& ctx, Id idx, const IR::Value& index,
                                  NonUniformKind kind) {
    if (index.IsImmediate() || !IsNonUniformSupported(ctx.profile, kind)) {
        return false;
    }
    DecorateNonUniform(ctx, idx);
    switch (kind) {
    case NonUniformKind::SampledImage:
        ctx.uses_nonuniform_sampled_image = true;
        break;
    case NonUniformKind::StorageImage:
        ctx.uses_nonuniform_storage_image = true;
        break;
    case NonUniformKind::UniformTexelBuffer:
        ctx.uses_nonuniform_uniform_texel_buffer = true;
        break;
    case NonUniformKind::StorageTexelBuffer:
        ctx.uses_nonuniform_storage_texel_buffer = true;
        break;
    }
    return true;
}

class ImageOperands {
public:
    // The Offset operand (not constant) is only valid with the gather instructions in Vulkan, the
    // other instructions can only have a constant offset
    [[maybe_unused]] static constexpr bool ImageSampleOffsetAllowed = false;
    [[maybe_unused]] static constexpr bool ImageGatherOffsetAllowed = true;
    [[maybe_unused]] static constexpr bool ImageFetchOffsetAllowed = false;
    [[maybe_unused]] static constexpr bool ImageGradientOffsetAllowed = false;

    explicit ImageOperands(EmitContext& ctx, bool has_bias, bool has_lod, bool has_lod_clamp,
                           Id lod, const IR::Value& offset) {
        if (has_bias) {
            const Id bias{has_lod_clamp ? ctx.OpCompositeExtract(ctx.F32[1], lod, 0) : lod};
            Add(spv::ImageOperandsMask::Bias, bias);
        }
        if (has_lod) {
            const Id lod_value{has_lod_clamp ? ctx.OpCompositeExtract(ctx.F32[1], lod, 0) : lod};
            Add(spv::ImageOperandsMask::Lod, lod_value);
        }
        AddOffset(ctx, offset, ImageSampleOffsetAllowed);
        if (has_lod_clamp) {
            const Id lod_clamp{has_bias ? ctx.OpCompositeExtract(ctx.F32[1], lod, 1) : lod};
            Add(spv::ImageOperandsMask::MinLod, lod_clamp);
        }
    }

    explicit ImageOperands(EmitContext& ctx, const IR::Value& offset, const IR::Value& offset2) {
        if (offset2.IsEmpty()) {
            AddOffset(ctx, offset, ImageGatherOffsetAllowed);
            return;
        }
        const std::array values{offset.InstRecursive(), offset2.InstRecursive()};
        if (!values[0]->AreAllArgsImmediates() || !values[1]->AreAllArgsImmediates()) {
            LOG_WARNING(Shader_SPIRV, "Not all arguments in PTP are immediate, ignoring");
            return;
        }
        const IR::Opcode opcode{values[0]->GetOpcode()};
        if (opcode != values[1]->GetOpcode() || opcode != IR::Opcode::CompositeConstructU32x4) {
            throw LogicError("Invalid PTP arguments");
        }
        auto read{[&](unsigned int a, unsigned int b) {
            return static_cast<s32>(values[a]->Arg(b).U32());
        }};

        const Id offsets{ctx.ConstantComposite(
            ctx.TypeArray(ctx.S32[2], ctx.Const(4U)), ctx.SConst(read(0, 0), read(0, 1)),
            ctx.SConst(read(0, 2), read(0, 3)), ctx.SConst(read(1, 0), read(1, 1)),
            ctx.SConst(read(1, 2), read(1, 3)))};
        Add(spv::ImageOperandsMask::ConstOffsets, offsets);
    }

    explicit ImageOperands(Id lod, Id ms) {
        if (Sirit::ValidId(lod)) {
            Add(spv::ImageOperandsMask::Lod, lod);
        }
        if (Sirit::ValidId(ms)) {
            Add(spv::ImageOperandsMask::Sample, ms);
        }
    }

    explicit ImageOperands(EmitContext& ctx, bool has_lod_clamp, Id derivates, u32 num_derivates,
                           const IR::Value& offset, Id lod_clamp) {
        if (!Sirit::ValidId(derivates)) {
            throw LogicError("Derivates must be present");
        }
        boost::container::static_vector<Id, 3> deriv_x_accum;
        boost::container::static_vector<Id, 3> deriv_y_accum;
        for (u32 i = 0; i < num_derivates; ++i) {
            deriv_x_accum.push_back(ctx.OpCompositeExtract(ctx.F32[1], derivates, i * 2));
            deriv_y_accum.push_back(ctx.OpCompositeExtract(ctx.F32[1], derivates, i * 2 + 1));
        }
        const Id derivates_X{ctx.OpCompositeConstruct(
            ctx.F32[num_derivates], std::span{deriv_x_accum.data(), deriv_x_accum.size()})};
        const Id derivates_Y{ctx.OpCompositeConstruct(
            ctx.F32[num_derivates], std::span{deriv_y_accum.data(), deriv_y_accum.size()})};
        Add(spv::ImageOperandsMask::Grad, derivates_X, derivates_Y);
        AddOffset(ctx, offset, ImageGradientOffsetAllowed);
        if (has_lod_clamp) {
            Add(spv::ImageOperandsMask::MinLod, lod_clamp);
        }
    }

    /// The gradient of a 3D texture, the 6 derivatives come in a vector of 4 and a vector of 2:
    /// (dx.x, dy.x, dx.y, dy.y) and (dx.z, dy.z)
    explicit ImageOperands(EmitContext& ctx, Id derivates_1, Id derivates_2, bool has_lod_clamp,
                           Id lod_clamp) {
        if (!Sirit::ValidId(derivates_1) || !Sirit::ValidId(derivates_2)) {
            throw LogicError("Derivates must be present");
        }
        boost::container::static_vector<Id, 3> deriv_x_accum{
            ctx.OpCompositeExtract(ctx.F32[1], derivates_1, 0),
            ctx.OpCompositeExtract(ctx.F32[1], derivates_1, 2),
            ctx.OpCompositeExtract(ctx.F32[1], derivates_2, 0),
        };
        boost::container::static_vector<Id, 3> deriv_y_accum{
            ctx.OpCompositeExtract(ctx.F32[1], derivates_1, 1),
            ctx.OpCompositeExtract(ctx.F32[1], derivates_1, 3),
            ctx.OpCompositeExtract(ctx.F32[1], derivates_2, 1),
        };
        const Id derivates_X{ctx.OpCompositeConstruct(
            ctx.F32[3], std::span{deriv_x_accum.data(), deriv_x_accum.size()})};
        const Id derivates_Y{ctx.OpCompositeConstruct(
            ctx.F32[3], std::span{deriv_y_accum.data(), deriv_y_accum.size()})};
        Add(spv::ImageOperandsMask::Grad, derivates_X, derivates_Y);
        if (has_lod_clamp) {
            Add(spv::ImageOperandsMask::MinLod, lod_clamp);
        }
    }

    std::span<const Id> Span() const noexcept {
        return std::span{operands.data(), operands.size()};
    }

    std::optional<spv::ImageOperandsMask> MaskOptional() const noexcept {
        return mask != spv::ImageOperandsMask{} ? std::make_optional(mask) : std::nullopt;
    }

    spv::ImageOperandsMask Mask() const noexcept {
        return mask;
    }

private:
    void AddOffset(EmitContext& ctx, const IR::Value& offset, bool runtime_offset_allowed) {
        if (offset.IsEmpty()) {
            return;
        }
        if (offset.IsImmediate()) {
            Add(spv::ImageOperandsMask::ConstOffset, ctx.SConst(static_cast<s32>(offset.U32())));
            return;
        }
        IR::Inst* const inst{offset.InstRecursive()};
        if (inst->AreAllArgsImmediates()) {
            switch (inst->GetOpcode()) {
            case IR::Opcode::CompositeConstructU32x2:
                Add(spv::ImageOperandsMask::ConstOffset,
                    ctx.SConst(static_cast<s32>(inst->Arg(0).U32()),
                               static_cast<s32>(inst->Arg(1).U32())));
                return;
            case IR::Opcode::CompositeConstructU32x3:
                Add(spv::ImageOperandsMask::ConstOffset,
                    ctx.SConst(static_cast<s32>(inst->Arg(0).U32()),
                               static_cast<s32>(inst->Arg(1).U32()),
                               static_cast<s32>(inst->Arg(2).U32())));
                return;
            case IR::Opcode::CompositeConstructU32x4:
                Add(spv::ImageOperandsMask::ConstOffset,
                    ctx.SConst(static_cast<s32>(inst->Arg(0).U32()),
                               static_cast<s32>(inst->Arg(1).U32()),
                               static_cast<s32>(inst->Arg(2).U32()),
                               static_cast<s32>(inst->Arg(3).U32())));
                return;
            default:
                break;
            }
        }
        if (runtime_offset_allowed) {
            Add(spv::ImageOperandsMask::Offset, ctx.Def(offset));
        } else {
            LOG_WARNING(Shader_SPIRV, "Dropping a non constant texture offset, the host can't use it");
        }
    }

    void Add(spv::ImageOperandsMask new_mask, Id value) {
        mask = static_cast<spv::ImageOperandsMask>(static_cast<unsigned>(mask) |
                                                   static_cast<unsigned>(new_mask));
        operands.push_back(value);
    }

    void Add(spv::ImageOperandsMask new_mask, Id value_1, Id value_2) {
        mask = static_cast<spv::ImageOperandsMask>(static_cast<unsigned>(mask) |
                                                   static_cast<unsigned>(new_mask));
        operands.push_back(value_1);
        operands.push_back(value_2);
    }

    boost::container::static_vector<Id, 4> operands;
    spv::ImageOperandsMask mask{};
};

Id Texture(EmitContext& ctx, IR::TextureInstInfo info, [[maybe_unused]] const IR::Value& index) {
    const TextureDefinition& def{ctx.textures.at(info.descriptor_index)};
    if (def.count > 1) {
        const Id idx{index.IsImmediate() ? ctx.Const(index.U32()) : ctx.Def(index)};
        const bool non_uniform{MarkNonUniform(ctx, idx, index, NonUniformKind::SampledImage)};
        const Id pointer{ctx.OpAccessChain(def.pointer_type, def.id, idx)};
        const Id object{ctx.OpLoad(def.sampled_type, pointer)};
        if (non_uniform) {
            DecorateNonUniform(ctx, pointer);
            DecorateNonUniform(ctx, object);
        }
        return object;
    } else {
        return ctx.OpLoad(def.sampled_type, def.id);
    }
}

Id TextureImage(EmitContext& ctx, IR::TextureInstInfo info, const IR::Value& index) {
    if (info.type == TextureType::Buffer) {
        const TextureBufferDefinition& def{ctx.texture_buffers.at(info.descriptor_index)};
        if (def.count > 1) {
            const Id idx{index.IsImmediate() ? ctx.Const(index.U32()) : ctx.Def(index)};
            const bool non_uniform{
                MarkNonUniform(ctx, idx, index, NonUniformKind::UniformTexelBuffer)};
            const Id ptr{ctx.OpAccessChain(def.pointer_type, def.id, idx)};
            const Id object{ctx.OpLoad(ctx.sampled_texture_buffer_type, ptr)};
            const Id image{ctx.OpImage(ctx.image_buffer_type, object)};
            if (non_uniform) {
                DecorateNonUniform(ctx, ptr);
                DecorateNonUniform(ctx, object);
                DecorateNonUniform(ctx, image);
            }
            return image;
        }
        const Id sampler_id{def.id};
        const Id id{ctx.OpLoad(ctx.sampled_texture_buffer_type, sampler_id)};
        return ctx.OpImage(ctx.image_buffer_type, id);
    } else {
        const TextureDefinition& def{ctx.textures.at(info.descriptor_index)};
        if (def.count > 1) {
            const Id idx{index.IsImmediate() ? ctx.Const(index.U32()) : ctx.Def(index)};
            const bool non_uniform{MarkNonUniform(ctx, idx, index, NonUniformKind::SampledImage)};
            const Id ptr{ctx.OpAccessChain(def.pointer_type, def.id, idx)};
            const Id object{ctx.OpLoad(def.sampled_type, ptr)};
            const Id image{ctx.OpImage(def.image_type, object)};
            if (non_uniform) {
                DecorateNonUniform(ctx, ptr);
                DecorateNonUniform(ctx, object);
                DecorateNonUniform(ctx, image);
            }
            return image;
        }
        return ctx.OpImage(def.image_type, ctx.OpLoad(def.sampled_type, def.id));
    }
}

std::pair<Id, bool> Image(EmitContext& ctx, const IR::Value& index, IR::TextureInstInfo info) {
    if (info.type == TextureType::Buffer) {
        const ImageBufferDefinition def{ctx.image_buffers.at(info.descriptor_index)};
        if (def.count > 1) {
            const Id idx{index.IsImmediate() ? ctx.Const(index.U32()) : ctx.Def(index)};
            const bool non_uniform{
                MarkNonUniform(ctx, idx, index, NonUniformKind::StorageTexelBuffer)};
            const Id ptr{ctx.OpAccessChain(def.pointer_type, def.id, idx)};
            const Id image{ctx.OpLoad(def.image_type, ptr)};
            if (non_uniform) {
                DecorateNonUniform(ctx, ptr);
                DecorateNonUniform(ctx, image);
            }
            return {image, def.is_integer};
        }
        return {ctx.OpLoad(def.image_type, def.id), def.is_integer};
    } else {
        const ImageDefinition def{ctx.images.at(info.descriptor_index)};
        if (def.count > 1) {
            const Id idx{index.IsImmediate() ? ctx.Const(index.U32()) : ctx.Def(index)};
            const bool non_uniform{MarkNonUniform(ctx, idx, index, NonUniformKind::StorageImage)};
            const Id ptr{ctx.OpAccessChain(def.pointer_type, def.id, idx)};
            const Id image{ctx.OpLoad(def.image_type, ptr)};
            if (non_uniform) {
                DecorateNonUniform(ctx, ptr);
                DecorateNonUniform(ctx, image);
            }
            return {image, def.is_integer};
        }
        return {ctx.OpLoad(def.image_type, def.id), def.is_integer};
    }
}

Id Decorate(EmitContext& ctx, IR::Inst* inst, Id sample) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    if (info.relaxed_precision != 0) {
        ctx.Decorate(sample, spv::Decoration::RelaxedPrecision);
    }
    return sample;
}

template <typename MethodPtrType, typename... Args>
Id Emit(MethodPtrType sparse_ptr, MethodPtrType non_sparse_ptr, EmitContext& ctx, IR::Inst* inst,
        Id result_type, Args&&... args) {
    IR::Inst* const sparse{inst->GetAssociatedPseudoOperation(IR::Opcode::GetSparseFromOp)};
    if (!sparse) {
        return Decorate(ctx, inst, (ctx.*non_sparse_ptr)(result_type, std::forward<Args>(args)...));
    }
    const Id struct_type{ctx.TypeStruct(ctx.U32[1], result_type)};
    const Id sample{(ctx.*sparse_ptr)(struct_type, std::forward<Args>(args)...)};
    const Id resident_code{ctx.OpCompositeExtract(ctx.U32[1], sample, 0U)};
    sparse->SetDefinition(ctx.OpImageSparseTexelsResident(ctx.U1, resident_code));
    sparse->Invalidate();
    Decorate(ctx, inst, sample);
    return ctx.OpCompositeExtract(result_type, sample, 1U);
}

Id IsScaled(EmitContext& ctx, const IR::Value& index, Id member_index, u32 base_index) {
    const Id push_constant_u32{ctx.TypePointer(spv::StorageClass::PushConstant, ctx.U32[1])};
    Id bit{};
    if (index.IsImmediate()) {
        // Use BitwiseAnd instead of BitfieldExtract for better codegen on Nvidia OpenGL.
        // LOP32I.NZ is used to set the predicate rather than BFE+ISETP.
        const u32 index_value{index.U32() + base_index};
        const Id word_index{ctx.Const(index_value / 32)};
        const Id bit_index_mask{ctx.Const(1u << (index_value % 32))};
        const Id pointer{ctx.OpAccessChain(push_constant_u32, ctx.rescaling_push_constants,
                                           member_index, word_index)};
        const Id word{ctx.OpLoad(ctx.U32[1], pointer)};
        bit = ctx.OpBitwiseAnd(ctx.U32[1], word, bit_index_mask);
    } else {
        Id index_value{ctx.Def(index)};
        if (base_index != 0) {
            index_value = ctx.OpIAdd(ctx.U32[1], index_value, ctx.Const(base_index));
        }
        const Id bit_index{ctx.OpBitwiseAnd(ctx.U32[1], index_value, ctx.Const(31u))};
        bit = ctx.OpBitFieldUExtract(ctx.U32[1], index_value, bit_index, ctx.Const(1u));
    }
    return ctx.OpINotEqual(ctx.U1, bit, ctx.u32_zero_value);
}

Id BitTest(EmitContext& ctx, Id mask, Id bit) {
    const Id shifted{ctx.OpShiftRightLogical(ctx.U32[1], mask, bit)};
    const Id bit_value{ctx.OpBitwiseAnd(ctx.U32[1], shifted, ctx.Const(1u))};
    return ctx.OpINotEqual(ctx.U1, bit_value, ctx.u32_zero_value);
}

/// @return true if the sampled texture has an integer format, its texels are read as U32
bool IsTextureInteger(EmitContext& ctx, const IR::TextureInstInfo& info) {
    if (info.type == TextureType::Buffer) {
        return false;
    }
    return ctx.textures.at(info.descriptor_index).is_integer;
}

/// @return true if the sampled texture is multisampled
bool IsTextureMsaa(EmitContext& ctx, const IR::TextureInstInfo& info) {
    if (info.type == TextureType::Buffer) {
        return false;
    }
    return ctx.textures.at(info.descriptor_index).is_multisample;
}

/// The IR sees the texels as F32, an integer texture returns U32 that are bitcast
Id TexelType(EmitContext& ctx, bool is_integer) {
    return is_integer ? ctx.U32[4] : ctx.F32[4];
}

Id TexelResult(EmitContext& ctx, bool is_integer, Id texel) {
    return is_integer ? ctx.OpBitcast(ctx.F32[4], texel) : texel;
}

/// OpImageFetch can't have an Offset operand in Vulkan, the offset is added to the coordinates
void AddOffsetToCoordinates(EmitContext& ctx, const IR::TextureInstInfo& info, Id& coords,
                            Id offset) {
    if (!Sirit::ValidId(offset)) {
        return;
    }

    Id result_type{};
    switch (info.type) {
    case TextureType::Buffer:
    case TextureType::Color1D: {
        result_type = ctx.U32[1];
        break;
    }
    case TextureType::ColorArray1D:
        offset = ctx.OpCompositeConstruct(ctx.U32[2], offset, ctx.u32_zero_value);
        [[fallthrough]];
    case TextureType::Color2D:
    case TextureType::Color2DRect: {
        result_type = ctx.U32[2];
        break;
    }
    case TextureType::ColorArray2D:
        offset = ctx.OpCompositeConstruct(ctx.U32[3], ctx.OpCompositeExtract(ctx.U32[1], offset, 0),
                                          ctx.OpCompositeExtract(ctx.U32[1], offset, 1),
                                          ctx.u32_zero_value);
        [[fallthrough]];
    case TextureType::Color3D: {
        result_type = ctx.U32[3];
        break;
    }
    case TextureType::ColorCube:
    case TextureType::ColorArrayCube:
        return;
    }
    coords = ctx.OpIAdd(result_type, coords, offset);
}
} // Anonymous namespace

Id EmitBindlessImageSampleImplicitLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageSampleExplicitLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageSampleDrefImplicitLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageSampleDrefExplicitLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageGather(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageGatherDref(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageFetch(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageQueryDimensions(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageQueryLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageGradient(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageRead(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBindlessImageWrite(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageSampleImplicitLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageSampleExplicitLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageSampleDrefImplicitLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageSampleDrefExplicitLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageGather(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageGatherDref(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageFetch(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageQueryDimensions(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageQueryLod(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageGradient(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageRead(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitBoundImageWrite(EmitContext&) {
    throw LogicError("Unreachable instruction");
}

Id EmitImageSampleImplicitLod(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                              Id bias_lc, const IR::Value& offset) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const bool is_integer{IsTextureInteger(ctx, info)};
    if (ctx.stage == Stage::Fragment) {
        const ImageOperands operands(ctx, info.has_bias != 0, false, info.has_lod_clamp != 0,
                                     bias_lc, offset);
        return TexelResult(
            ctx, is_integer,
            Emit(&EmitContext::OpImageSparseSampleImplicitLod,
                 &EmitContext::OpImageSampleImplicitLod, ctx, inst, TexelType(ctx, is_integer),
                 Texture(ctx, info, index), coords, operands.MaskOptional(), operands.Span()));
    } else {
        // We can't use implicit lods on non-fragment stages on SPIR-V. Maxwell hardware behaves as
        // if the lod was explicitly zero.  This may change on Turing with implicit compute
        // derivatives
        const Id lod{ctx.Const(0.0f)};
        const ImageOperands operands(ctx, false, true, info.has_lod_clamp != 0, lod, offset);
        return TexelResult(
            ctx, is_integer,
            Emit(&EmitContext::OpImageSparseSampleExplicitLod,
                 &EmitContext::OpImageSampleExplicitLod, ctx, inst, TexelType(ctx, is_integer),
                 Texture(ctx, info, index), coords, operands.Mask(), operands.Span()));
    }
}

Id EmitImageSampleExplicitLod(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                              Id lod, const IR::Value& offset) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const bool is_integer{IsTextureInteger(ctx, info)};
    const ImageOperands operands(ctx, false, true, false, lod, offset);
    Id texel{Emit(&EmitContext::OpImageSparseSampleExplicitLod,
                  &EmitContext::OpImageSampleExplicitLod, ctx, inst, TexelType(ctx, is_integer),
                  Texture(ctx, info, index), coords, operands.Mask(), operands.Span())};
    if (!is_integer && ctx.profile.fix_bloom_effects) {
        // HACK: hides the over-bright bloom some games have on some GPUs
        texel = ctx.OpVectorTimesScalar(ctx.F32[4], texel, ctx.Const(0.98f));
    }
    return TexelResult(ctx, is_integer, texel);
}

Id EmitImageSampleDrefImplicitLod(EmitContext& ctx, IR::Inst* inst, const IR::Value& index,
                                  Id coords, Id dref, Id bias_lc, const IR::Value& offset) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    if (ctx.stage == Stage::Fragment) {
        const ImageOperands operands(ctx, info.has_bias != 0, false, info.has_lod_clamp != 0,
                                     bias_lc, offset);
        return Emit(&EmitContext::OpImageSparseSampleDrefImplicitLod,
                    &EmitContext::OpImageSampleDrefImplicitLod, ctx, inst, ctx.F32[1],
                    Texture(ctx, info, index), coords, dref, operands.MaskOptional(),
                    operands.Span());
    } else {
        // Implicit lods in compute behave on hardware as if sampling from LOD 0.
        // This check is to ensure all drivers behave this way.
        const Id lod{ctx.Const(0.0f)};
        const ImageOperands operands(ctx, false, true, false, lod, offset);
        return Emit(&EmitContext::OpImageSparseSampleDrefExplicitLod,
                    &EmitContext::OpImageSampleDrefExplicitLod, ctx, inst, ctx.F32[1],
                    Texture(ctx, info, index), coords, dref, operands.Mask(), operands.Span());
    }
}

Id EmitImageSampleDrefExplicitLod(EmitContext& ctx, IR::Inst* inst, const IR::Value& index,
                                  Id coords, Id dref, Id lod, const IR::Value& offset) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const ImageOperands operands(ctx, false, true, false, lod, offset);
    return Emit(&EmitContext::OpImageSparseSampleDrefExplicitLod,
                &EmitContext::OpImageSampleDrefExplicitLod, ctx, inst, ctx.F32[1],
                Texture(ctx, info, index), coords, dref, operands.Mask(), operands.Span());
}

Id EmitImageGather(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                   const IR::Value& offset, const IR::Value& offset2) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const bool is_integer{IsTextureInteger(ctx, info)};
    const ImageOperands operands(ctx, offset, offset2);
    return TexelResult(
        ctx, is_integer,
        Emit(&EmitContext::OpImageSparseGather, &EmitContext::OpImageGather, ctx, inst,
             TexelType(ctx, is_integer), Texture(ctx, info, index), coords,
             ctx.Const(info.gather_component), operands.MaskOptional(), operands.Span()));
}

Id EmitImageGatherDref(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                       const IR::Value& offset, const IR::Value& offset2, Id dref) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const ImageOperands operands(ctx, offset, offset2);
    return Emit(&EmitContext::OpImageSparseDrefGather, &EmitContext::OpImageDrefGather, ctx, inst,
                ctx.F32[4], Texture(ctx, info, index), coords, dref, operands.MaskOptional(),
                operands.Span());
}

Id EmitImageFetch(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords, Id offset,
                  Id lod, Id ms) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    AddOffsetToCoordinates(ctx, info, coords, offset);
    if (info.type == TextureType::Buffer) {
        lod = Id{};
    }
    if (Sirit::ValidId(ms)) {
        // This image is multisampled, lod must be implicit
        lod = Id{};
    }
    const bool is_integer{IsTextureInteger(ctx, info)};
    const ImageOperands operands(lod, ms);
    return TexelResult(
        ctx, is_integer,
        Emit(&EmitContext::OpImageSparseFetch, &EmitContext::OpImageFetch, ctx, inst,
             TexelType(ctx, is_integer), TextureImage(ctx, info, index), coords,
             operands.MaskOptional(), operands.Span()));
}

Id EmitImageQueryDimensions(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id lod) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const Id image{TextureImage(ctx, info, index)};
    const Id zero{ctx.u32_zero_value};
    // A multisampled image has a single level and can't be queried with a lod
    const bool is_msaa{IsTextureMsaa(ctx, info)};
    const auto mips{[&]() -> Id {
        return is_msaa ? ctx.Const(1u) : ctx.OpImageQueryLevels(ctx.U32[1], image);
    }};
    const auto size{[&](Id type) -> Id {
        return is_msaa ? ctx.OpImageQuerySize(type, image) : ctx.OpImageQuerySizeLod(type, image, lod);
    }};
    switch (info.type) {
    case TextureType::Color1D:
        return ctx.OpCompositeConstruct(ctx.U32[4], size(ctx.U32[1]), zero, zero, mips());
    case TextureType::ColorArray1D:
    case TextureType::Color2D:
    case TextureType::ColorCube:
    case TextureType::Color2DRect:
        return ctx.OpCompositeConstruct(ctx.U32[4], size(ctx.U32[2]), zero, mips());
    case TextureType::ColorArray2D:
    case TextureType::Color3D:
    case TextureType::ColorArrayCube:
        return ctx.OpCompositeConstruct(ctx.U32[4], size(ctx.U32[3]), mips());
    case TextureType::Buffer:
        return ctx.OpCompositeConstruct(ctx.U32[4], ctx.OpImageQuerySize(ctx.U32[1], image), zero,
                                        zero, mips());
    }
    throw LogicError("Unspecified image type {}", info.type.Value());
}

Id EmitImageQueryLod(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const Id zero{ctx.f32_zero_value};
    const Id sampler{Texture(ctx, info, index)};
    return ctx.OpCompositeConstruct(ctx.F32[4], ctx.OpImageQueryLod(ctx.F32[2], sampler, coords),
                                    zero, zero);
}

Id EmitImageGradient(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords,
                     Id derivates, const IR::Value& offset, Id lod_clamp) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    // With 3 derivatives (a 3D texture) the "offset" is the second vector of derivatives
    const ImageOperands operands =
        info.num_derivates == 3
            ? ImageOperands(ctx, derivates, ctx.Def(offset), info.has_lod_clamp != 0, lod_clamp)
            : ImageOperands(ctx, info.has_lod_clamp != 0, derivates, info.num_derivates, offset,
                            lod_clamp);
    const bool is_integer{IsTextureInteger(ctx, info)};
    return TexelResult(
        ctx, is_integer,
        Emit(&EmitContext::OpImageSparseSampleExplicitLod,
             &EmitContext::OpImageSampleExplicitLod, ctx, inst, TexelType(ctx, is_integer),
             Texture(ctx, info, index), coords, operands.Mask(), operands.Span()));
}

Id EmitImageRead(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    if (info.image_format == ImageFormat::Typeless && !ctx.profile.support_typeless_image_loads) {
        LOG_WARNING(Shader_SPIRV, "Typeless image read not supported by host");
        return ctx.ConstantNull(ctx.U32[4]);
    }
    const auto [image, is_integer] = Image(ctx, index, info);
    const Id result_type{is_integer ? ctx.U32[4] : ctx.F32[4]};
    Id color{Emit(&EmitContext::OpImageSparseRead, &EmitContext::OpImageRead, ctx, inst,
                  result_type, image, coords, std::nullopt, std::span<const Id>{})};
    if (!is_integer) {
        // The IR sees the texels as raw 32 bit values
        color = ctx.OpBitcast(ctx.U32[4], color);
    }
    return color;
}

void EmitImageWrite(EmitContext& ctx, IR::Inst* inst, const IR::Value& index, Id coords, Id color) {
    const auto info{inst->Flags<IR::TextureInstInfo>()};
    const auto [image, is_integer] = Image(ctx, index, info);
    if (!is_integer) {
        color = ctx.OpBitcast(ctx.F32[4], color);
    }
    ctx.OpImageWrite(image, coords, color);
}

Id EmitIsTextureScaled(EmitContext& ctx, const IR::Value& index) {
    if (ctx.profile.unified_descriptor_binding) {
        const Id member_index{ctx.Const(ctx.rescaling_textures_member_index)};
        return IsScaled(ctx, index, member_index, ctx.texture_rescaling_index);
    } else {
        const Id composite{ctx.OpLoad(ctx.F32[4], ctx.rescaling_uniform_constant)};
        const Id mask_f32{ctx.OpCompositeExtract(ctx.F32[1], composite, 0u)};
        const Id mask{ctx.OpBitcast(ctx.U32[1], mask_f32)};
        return BitTest(ctx, mask, ctx.Def(index));
    }
}

Id EmitIsImageScaled(EmitContext& ctx, const IR::Value& index) {
    if (ctx.profile.unified_descriptor_binding) {
        const Id member_index{ctx.Const(ctx.rescaling_images_member_index)};
        return IsScaled(ctx, index, member_index, ctx.image_rescaling_index);
    } else {
        const Id composite{ctx.OpLoad(ctx.F32[4], ctx.rescaling_uniform_constant)};
        const Id mask_f32{ctx.OpCompositeExtract(ctx.F32[1], composite, 1u)};
        const Id mask{ctx.OpBitcast(ctx.U32[1], mask_f32)};
        return BitTest(ctx, mask, ctx.Def(index));
    }
}

} // namespace Shader::Backend::SPIRV
