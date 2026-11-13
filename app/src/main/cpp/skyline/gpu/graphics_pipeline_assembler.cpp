// SPDX-License-Identifier: MPL-2.0
// Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <boost/functional/hash.hpp>
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <gpu.h>
#include "graphics_pipeline_assembler.h"
#include "breadcrumbs.h"
#include "trait_manager.h"

namespace skyline::gpu {
    /**
     * @brief Unique header serialized into the pipeline cache filename as a hexdump to identify a particular driver
     */
    struct PipelineCacheFileNameHeader {
        u32 vendorId; //!< The driver reported vendor ID
        u32 deviceId; //!< The driver reported device ID
        u32 driverVersion; //!< The driver reported version
        std::array<u8, VK_UUID_SIZE> uuid; //!< The driver reported pipeline cache UUID

        PipelineCacheFileNameHeader(const TraitManager &traits)
            : vendorId{traits.vendorId},
              deviceId{traits.deviceId},
              driverVersion{traits.driverVersion},
              uuid{traits.pipelineCacheUuid} {}

        std::string HexDump() {
            return util::HexDump(span<u8>{reinterpret_cast<u8 *>(this), sizeof(PipelineCacheFileNameHeader)});
        }
    };

    /**
     * @brief Header that precedes serialized pipeline cache data in the pipeline cache file
     */
    struct PipelineCacheFileDataHeader {
        u64 size;
        u64 hash;
        u8 data[];
    };
    static_assert(sizeof(PipelineCacheFileDataHeader) == 0x10);

    static vk::raii::PipelineCache DeserialisePipelineCache(GPU &gpu, std::string_view pipelineCacheDir) {
        std::filesystem::create_directories(pipelineCacheDir);
        PipelineCacheFileNameHeader expectedFilenameHeader{gpu.traits};
        std::filesystem::path path{std::filesystem::path{pipelineCacheDir} / expectedFilenameHeader.HexDump()};

        if (!std::filesystem::exists(path))
            return {gpu.vkDevice, vk::PipelineCacheCreateInfo{}};

        std::ifstream stream{path, std::ios::binary};
        if (stream.fail()) {
            LOGW("Failed to open Vulkan pipeline cache!");
            return {gpu.vkDevice, vk::PipelineCacheCreateInfo{}};
        }

        PipelineCacheFileDataHeader header{};
        stream.read(reinterpret_cast<char *>(&header), sizeof(PipelineCacheFileDataHeader));

        std::vector<u8> readData(header.size);
        stream.read(reinterpret_cast<char *>(readData.data()), static_cast<std::streamsize>(header.size));

        if (header.hash != XXH64(readData.data(), readData.size(), 0)) {
            LOGW("Ignoring invalid pipeline cache file!");
            return {gpu.vkDevice, vk::PipelineCacheCreateInfo{}};
        }

        return {gpu.vkDevice, vk::PipelineCacheCreateInfo{
            .initialDataSize = readData.size(),
            .pInitialData = readData.data(),
        }};
    }

    static void SerialisePipelineCache(GPU &gpu, std::string_view pipelineCacheDir, span<u8> data) {
        PipelineCacheFileNameHeader expectedFilenameHeader{gpu.traits};
        std::filesystem::path path{std::filesystem::path{pipelineCacheDir} / expectedFilenameHeader.HexDump()};

        PipelineCacheFileDataHeader header{
            .size = data.size(),
            .hash = XXH64(data.data(), data.size(), 0)
        };
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        if (stream.fail()) {
            LOGW("Failed to write Vulkan pipeline cache!");
            return;
        }

        stream.write(reinterpret_cast<char *>(&header), sizeof(PipelineCacheFileDataHeader));
        stream.write(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));

        LOGI("Wrote Vulkan pipeline cache to {} (size: 0x{:X} bytes)", path.string(), data.size());
    }

    GraphicsPipelineAssembler::GraphicsPipelineAssembler(GPU &gpu, std::string_view pipelineCacheDir)
        : gpu{gpu},
          vkPipelineCache{DeserialisePipelineCache(gpu, pipelineCacheDir)},
          pool{gpu.traits.quirks.brokenMultithreadedPipelineCompilation ? 1U : 0U},
          pipelineCacheDir{pipelineCacheDir} {}

    #define VEC_CPY(pointer, size) state.pointer, state.pointer + state.size

    GraphicsPipelineAssembler::PipelineDescription::PipelineDescription(const GraphicsPipelineAssembler::PipelineState &state)
        : shaderStages(state.shaderStages.begin(), state.shaderStages.end()),
          vertexState(state.vertexState),
          vertexBindings(VEC_CPY(VertexInputState().pVertexBindingDescriptions, VertexInputState().vertexBindingDescriptionCount)),
          vertexAttributes(VEC_CPY(VertexInputState().pVertexAttributeDescriptions, VertexInputState().vertexAttributeDescriptionCount)),
          vertexDivisors(VEC_CPY(VertexDivisorState().pVertexBindingDivisors, VertexDivisorState().vertexBindingDivisorCount)),
          inputAssemblyState(state.inputAssemblyState),
          tessellationState(state.tessellationState),
          viewportState(state.viewportState),
          viewports(VEC_CPY(viewportState.pViewports, viewportState.viewportCount)),
          scissors(VEC_CPY(viewportState.pScissors, viewportState.scissorCount)),
          rasterizationState(state.rasterizationState),
          multisampleState(state.multisampleState),
          depthStencilState(state.depthStencilState),
          colorBlendState(state.colorBlendState),
          dynamicStates(VEC_CPY(dynamicState.pDynamicStates, dynamicState.dynamicStateCount)),
          dynamicState(state.dynamicState),
          colorBlendAttachments(VEC_CPY(colorBlendState.pAttachments, colorBlendState.attachmentCount)) {
        auto &vertexInputState{vertexState.get<vk::PipelineVertexInputStateCreateInfo>()};
        vertexInputState.pVertexBindingDescriptions = vertexBindings.data();
        vertexInputState.pVertexAttributeDescriptions = vertexAttributes.data();
        vertexState.get<vk::PipelineVertexInputDivisorStateCreateInfoEXT>().pVertexBindingDivisors = vertexDivisors.data();

        viewportState.pViewports = viewports.data();
        viewportState.pScissors = scissors.data();

        colorBlendState.pAttachments = colorBlendAttachments.data();

        dynamicState.pDynamicStates = dynamicStates.data();

        for (auto &colorFormat : state.colorFormats)
            colorFormats.emplace_back(colorFormat);

        depthStencilFormat = state.depthStencilFormat;
        sampleCount = state.sampleCount;
        destroyShaderModules = state.destroyShaderModules;
    }

    #undef VEC_CPY

    namespace {
        /**
         * @brief A vertex shader that writes a position outside of the clip volume, everything drawn with it is clipped away (SPIR-V 1.0, assembled by hand because it never changes)
         */
        constexpr u32 DrawNothingVertexShader[]{
            0x07230203, 0x00010000, 0x00000000, 0x0000000C, 0x00000000, // Header: magic, version 1.0, generator, bound, schema
            0x00020011, 0x00000001, // OpCapability Shader
            0x0003000E, 0x00000000, 0x00000001, // OpMemoryModel Logical GLSL450
            0x0006000F, 0x00000000, 0x00000001, 0x6E69616D, 0x00000000, 0x00000007, // OpEntryPoint Vertex %1 "main" %7
            0x00040047, 0x00000007, 0x0000000B, 0x00000000, // OpDecorate %7 BuiltIn Position
            0x00020013, 0x00000002, // %2 = OpTypeVoid
            0x00030021, 0x00000003, 0x00000002, // %3 = OpTypeFunction %2
            0x00030016, 0x00000004, 0x00000020, // %4 = OpTypeFloat 32
            0x00040017, 0x00000005, 0x00000004, 0x00000004, // %5 = OpTypeVector %4 4
            0x00040020, 0x00000006, 0x00000003, 0x00000005, // %6 = OpTypePointer Output %5
            0x0004003B, 0x00000006, 0x00000007, 0x00000003, // %7 = OpVariable %6 Output
            0x0004002B, 0x00000004, 0x00000008, 0x40000000, // %8 = OpConstant %4 2.0
            0x0004002B, 0x00000004, 0x00000009, 0x3F800000, // %9 = OpConstant %4 1.0
            0x0007002C, 0x00000005, 0x0000000A, 0x00000008, 0x00000008, 0x00000008, 0x00000009, // %10 = OpConstantComposite %5 %8 %8 %8 %9
            0x00050036, 0x00000002, 0x00000001, 0x00000000, 0x00000003, // %1 = OpFunction %2 None %3
            0x000200F8, 0x0000000B, // %11 = OpLabel
            0x0003003E, 0x00000007, 0x0000000A, // OpStore %7 %10
            0x000100FD, // OpReturn
            0x00010038, // OpFunctionEnd
        };
    }

    vk::raii::Pipeline GraphicsPipelineAssembler::CreateDrawNothingPipeline(std::list<PipelineDescription>::iterator pipelineDescIt, vk::PipelineLayout pipelineLayout, vk::RenderPass renderPass) {
        vk::raii::ShaderModule shaderModule{gpu.vkDevice, vk::ShaderModuleCreateInfo{
            .codeSize = sizeof(DrawNothingVertexShader),
            .pCode = DrawNothingVertexShader,
        }};

        vk::PipelineShaderStageCreateInfo shaderStage{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = *shaderModule,
            .pName = "main",
        };

        // Without the other shader stages the topology can't be a patch list
        vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{pipelineDescIt->inputAssemblyState};
        if (inputAssemblyState.topology == vk::PrimitiveTopology::ePatchList)
            inputAssemblyState.topology = vk::PrimitiveTopology::eTriangleList;

        return gpu.vkDevice.createGraphicsPipeline(vkPipelineCache, vk::GraphicsPipelineCreateInfo{
            .pStages = &shaderStage,
            .stageCount = 1,
            .pVertexInputState = &pipelineDescIt->vertexState.get<vk::PipelineVertexInputStateCreateInfo>(),
            .pInputAssemblyState = &inputAssemblyState,
            .pViewportState = &pipelineDescIt->viewportState,
            .pRasterizationState = &pipelineDescIt->rasterizationState.get<vk::PipelineRasterizationStateCreateInfo>(),
            .pMultisampleState = &pipelineDescIt->multisampleState,
            .pDepthStencilState = &pipelineDescIt->depthStencilState,
            .pColorBlendState = &pipelineDescIt->colorBlendState,
            .pDynamicState = &pipelineDescIt->dynamicState,
            .layout = pipelineLayout,
            .renderPass = renderPass,
            .subpass = 0,
        });
    }

    vk::raii::Pipeline GraphicsPipelineAssembler::AssemblePipeline(std::list<PipelineDescription>::iterator pipelineDescIt, vk::PipelineLayout pipelineLayout) {
        boost::container::small_vector<vk::AttachmentDescription, 8> attachmentDescriptions;
        boost::container::small_vector<vk::AttachmentReference, 8> attachmentReferences;

        auto pushAttachment{[&](vk::Format format) {
            if (format != vk::Format::eUndefined) {
                attachmentDescriptions.push_back(vk::AttachmentDescription{
                    .format = format,
                    .samples = pipelineDescIt->sampleCount,
                    .loadOp = vk::AttachmentLoadOp::eLoad,
                    .storeOp = vk::AttachmentStoreOp::eStore,
                    .stencilLoadOp = vk::AttachmentLoadOp::eLoad,
                    .stencilStoreOp = vk::AttachmentStoreOp::eStore,
                    .initialLayout = vk::ImageLayout::eGeneral,
                    .finalLayout = vk::ImageLayout::eGeneral,
                    .flags = vk::AttachmentDescriptionFlagBits::eMayAlias
                });
                attachmentReferences.push_back(vk::AttachmentReference{
                    .attachment = static_cast<u32>(attachmentDescriptions.size() - 1),
                    .layout = vk::ImageLayout::eGeneral,
                });
            } else {
                attachmentReferences.push_back(vk::AttachmentReference{
                    .attachment = VK_ATTACHMENT_UNUSED,
                    .layout = vk::ImageLayout::eUndefined,
                });
            }
        }};

        vk::SubpassDescription subpassDescription{
            .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        };

        for (auto &colorAttachment : pipelineDescIt->colorFormats)
            pushAttachment(colorAttachment);

        if (pipelineDescIt->depthStencilFormat != vk::Format::eUndefined) {
            pushAttachment(pipelineDescIt->depthStencilFormat);

            subpassDescription.pColorAttachments = attachmentReferences.data();
            subpassDescription.colorAttachmentCount = static_cast<u32>(attachmentReferences.size() - 1);
            subpassDescription.pDepthStencilAttachment = &attachmentReferences.back();
        } else {
            subpassDescription.pColorAttachments = attachmentReferences.data();
            subpassDescription.colorAttachmentCount = static_cast<u32>(attachmentReferences.size());
        }

        vk::raii::RenderPass renderPass{gpu.vkDevice, vk::RenderPassCreateInfo{
            .attachmentCount = static_cast<u32>(attachmentDescriptions.size()),
            .pAttachments = attachmentDescriptions.data(),
            .subpassCount = 1,
            .pSubpasses = &subpassDescription,
        }};

        auto createPipeline{[&](span<const vk::PipelineShaderStageCreateInfo> shaderStages) {
            return gpu.vkDevice.createGraphicsPipeline(vkPipelineCache, vk::GraphicsPipelineCreateInfo{
            .pStages = shaderStages.data(),
            .stageCount = static_cast<u32>(shaderStages.size()),
            .pVertexInputState = &pipelineDescIt->vertexState.get<vk::PipelineVertexInputStateCreateInfo>(),
            .pInputAssemblyState = &pipelineDescIt->inputAssemblyState,
            .pTessellationState = &pipelineDescIt->tessellationState,
            .pViewportState = &pipelineDescIt->viewportState,
            .pRasterizationState = &pipelineDescIt->rasterizationState.get<vk::PipelineRasterizationStateCreateInfo>(),
            .pMultisampleState = &pipelineDescIt->multisampleState,
            .pDepthStencilState = &pipelineDescIt->depthStencilState,
            .pColorBlendState = &pipelineDescIt->colorBlendState,
            .pDynamicState = &pipelineDescIt->dynamicState,
            .layout = pipelineLayout,
            .renderPass = *renderPass,
            .subpass = 0,
            });
        }};

        vk::raii::Pipeline pipeline{nullptr};
        try {
            pipeline = createPipeline(pipelineDescIt->shaderStages);
        } catch (const vk::SystemError &e) {
            // A failed pipeline would otherwise kill the whole game: a geometry shader that had to be emulated without NV_geometry_shader_passthrough (seen on Adreno) is the known cause, so the pipeline is created again without that stage, which draws without whatever the shader did but keeps the game running
            boost::container::small_vector<vk::PipelineShaderStageCreateInfo, 5> withoutGeometry;
            for (const auto &shaderStage : pipelineDescIt->shaderStages)
                if (shaderStage.stage != vk::ShaderStageFlagBits::eGeometry)
                    withoutGeometry.push_back(shaderStage);

            bool recovered{};
            if (withoutGeometry.size() != pipelineDescIt->shaderStages.size()) {
                LOGW("Creating a pipeline with a geometry shader failed ({}), retrying without the geometry stage", e.what());
                try {
                    pipeline = createPipeline(withoutGeometry);
                    recovered = true;
                } catch (const vk::SystemError &) {}
            }

            if (!recovered) {
                // The driver rejected the shaders themselves (for example a SPIR-V module our shader recompiler generated that is invalid, No Man's Sky triggered this), the game can't do anything about that so the draws that use this pipeline are dropped instead of the whole emulator being killed
                LOGE("Creating a pipeline failed ({}), draws using it will draw nothing", e.what());
                if (gpu.shader)
                    for (const auto &shaderStage : pipelineDescIt->shaderStages)
                        gpu.shader->DumpFailedModule(shaderStage.module); // So the SPIR-V the driver complained about can be examined (shader_dumps/<titleId>/failed/)
                pipeline = CreateDrawNothingPipeline(pipelineDescIt, pipelineLayout, *renderPass);
            }
        }

        {
            std::string stages;
            for (const auto &shaderStage : pipelineDescIt->shaderStages)
                stages += fmt::format("{} ", vk::to_string(shaderStage.stage));
            Breadcrumbs::Get().Add("Pipeline compiled: {}", stages);
        }

        if (pipelineDescIt->destroyShaderModules)
            for (auto &shaderStage : pipelineDescIt->shaderStages)
                (*gpu.vkDevice).destroyShaderModule(shaderStage.module, nullptr,  *gpu.vkDevice.getDispatcher());

        std::scoped_lock lock{mutex};
        compilePendingDescs.erase(pipelineDescIt);
        if (compilationCallback)
            compilationCallback();

        return pipeline;
    }


    GraphicsPipelineAssembler::CompiledPipeline GraphicsPipelineAssembler::AssemblePipelineAsync(const PipelineState &state, span<const vk::DescriptorSetLayoutBinding> layoutBindings, span<const vk::PushConstantRange> pushConstantRanges, bool noPushDescriptors) {
        vk::raii::DescriptorSetLayout descriptorSetLayout{gpu.vkDevice, vk::DescriptorSetLayoutCreateInfo{
            .flags = vk::DescriptorSetLayoutCreateFlags{(!noPushDescriptors && gpu.traits.supportsPushDescriptors) ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR : vk::DescriptorSetLayoutCreateFlags{}},
            .pBindings = layoutBindings.data(),
            .bindingCount = static_cast<u32>(layoutBindings.size()),
        }};

        vk::raii::PipelineLayout pipelineLayout{gpu.vkDevice, vk::PipelineLayoutCreateInfo{
            .pSetLayouts = &*descriptorSetLayout,
            .setLayoutCount = 1,
            .pPushConstantRanges = pushConstantRanges.data(),
            .pushConstantRangeCount = static_cast<u32>(pushConstantRanges.size()),
        }};

        auto descIt{[this, &state]() {
            std::scoped_lock lock{mutex};
            compilePendingDescs.emplace_back(state);
            return std::prev(compilePendingDescs.end());
        }()};

        auto pipelineFuture{pool.submit(&GraphicsPipelineAssembler::AssemblePipeline, this, descIt, *pipelineLayout)};
        return CompiledPipeline{std::move(descriptorSetLayout), std::move(pipelineLayout), std::move(pipelineFuture)};
    }

    void GraphicsPipelineAssembler::WaitIdle() {
        pool.wait_for_tasks();
    }

    void GraphicsPipelineAssembler::WaitForPendingBelow(size_t maxPending) {
        while (true) {
            {
                std::scoped_lock lock{mutex};
                if (compilePendingDescs.size() < maxPending)
                    return;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
    }

    void GraphicsPipelineAssembler::SavePipelineCache() {
        std::ignore = pool.submit([this] () {
            std::vector<u8> rawData{vkPipelineCache.getData()};
            SerialisePipelineCache(gpu, pipelineCacheDir, rawData);
        });
    }

    void GraphicsPipelineAssembler::RegisterCompilationCallback(std::function<void()> callback) {
        if (compilationCallback)
            throw exception("A compilation callback is already registered");

        compilationCallback = std::move(callback);
    }

    void GraphicsPipelineAssembler::UnregisterCompilationCallback() {
        compilationCallback = {};
    }
}
