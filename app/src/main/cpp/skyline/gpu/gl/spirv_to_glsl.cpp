// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <vector>
#include <spirv_cross/spirv_glsl.hpp>
#include <common.h>
#include "spirv_to_glsl.h"

namespace skyline::gpu::gl {
    namespace {
        /**
         * @brief Gives the resources of a kind the next bindings of that kind, keeping the order of their original bindings
         */
        void Rebind(spirv_cross::CompilerGLSL &compiler, const spirv_cross::SmallVector<spirv_cross::Resource> &resources, u32 &counter) {
            std::vector<const spirv_cross::Resource *> sorted;
            for (const auto &resource : resources)
                sorted.push_back(&resource);

            std::ranges::sort(sorted, [&](const auto *a, const auto *b) {
                return compiler.get_decoration(a->id, spv::DecorationBinding) < compiler.get_decoration(b->id, spv::DecorationBinding);
            });

            for (const auto *resource : sorted) {
                compiler.unset_decoration(resource->id, spv::DecorationDescriptorSet);
                compiler.set_decoration(resource->id, spv::DecorationBinding, counter++);
            }
        }
    }

    std::string ConvertSpirvToGlslEs(const std::vector<u32> &spirv, Shader::Backend::Bindings &bindings) {
        try {
            spirv_cross::CompilerGLSL compiler{spirv.data(), spirv.size()};

            spirv_cross::CompilerGLSL::Options options;
            options.version = 320;
            options.es = true;
            options.vulkan_semantics = false; // gl_VertexID instead of gl_VertexIndex and the bindings of OpenGL
            options.separate_shader_objects = true;
            options.enable_420pack_extension = false; // Bindings in the shader are part of OpenGL ES 3.1
            options.force_flattened_io_blocks = false;
            options.vertex.fixup_clipspace = false;
            options.vertex.flip_vert_y = false;
            options.fragment.default_float_precision = spirv_cross::CompilerGLSL::Options::Highp;
            options.fragment.default_int_precision = spirv_cross::CompilerGLSL::Options::Highp;
            compiler.set_common_options(options);

            // Vulkan shaders have images and samplers combined already (the compiler makes combined image samplers), a shader with them separated would need build_combined_image_samplers()
            auto resources{compiler.get_shader_resources()};
            Rebind(compiler, resources.uniform_buffers, bindings.uniform_buffer);
            Rebind(compiler, resources.storage_buffers, bindings.storage_buffer);
            Rebind(compiler, resources.sampled_images, bindings.texture);
            Rebind(compiler, resources.storage_images, bindings.image);

            return compiler.compile();
        } catch (const spirv_cross::CompilerError &e) {
            throw exception("SPIRV-Cross failed: {}", e.what());
        }
    }
}
