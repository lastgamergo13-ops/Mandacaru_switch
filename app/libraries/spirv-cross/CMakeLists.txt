# SPIRV-Cross (vulkan-sdk-1.3.296.0), only the GLSL backend: the OpenGL ES backend of the emulator makes GLSL ES from the SPIR-V that the shader compiler makes for Vulkan (item E12 of the report)
cmake_minimum_required(VERSION 3.8)
project(spirv_cross_glsl CXX)

add_library(spirv_cross_glsl STATIC
        spirv_cross/spirv_cross.cpp
        spirv_cross/spirv_parser.cpp
        spirv_cross/spirv_cross_parsed_ir.cpp
        spirv_cross/spirv_cfg.cpp
        spirv_cross/spirv_cross_util.cpp
        spirv_cross/spirv_glsl.cpp)
target_include_directories(spirv_cross_glsl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(spirv_cross_glsl PUBLIC cxx_std_17)
set_target_properties(spirv_cross_glsl PROPERTIES POSITION_INDEPENDENT_CODE ON)
