// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <string>
#include <shader_compiler/backend/bindings.h>
#include <common.h>

namespace skyline::gpu::gl {
    /**
     * @brief Makes GLSL ES 3.20 from the SPIR-V that the shader compiler makes for Vulkan (with SPIRV-Cross), for the OpenGL ES backend (item E12 of the report)
     * @param bindings The bindings of the pipeline: SPIR-V has the bindings of all the kinds of resources in one sequence and OpenGL ES has one sequence for each kind, so the bindings of the shader are
     *        replaced with the next ones of each kind (uniform buffers, storage buffers, textures and images), in the order of the original bindings. The same counters are used for the shaders of a pipeline,
     *        one after the other, so the bindings of the stages don't overlap
     * @note Throws skyline::exception if SPIRV-Cross can't convert the shader
     */
    std::string ConvertSpirvToGlslEs(const std::vector<u32> &spirv, Shader::Backend::Bindings &bindings);
}
