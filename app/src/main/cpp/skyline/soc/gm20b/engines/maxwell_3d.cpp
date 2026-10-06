// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
// Copyright © 2018-2020 fincs (https://github.com/devkitPro/deko3d)

#include <boost/preprocessor/repeat.hpp>
#include <gpu/interconnect/command_executor.h>
#include <soc/gm20b/channel.h>
#include <soc.h>
#include "maxwell/types.h"
#include "maxwell_3d.h"

namespace skyline::soc::gm20b::engine::maxwell3d {
    #define REGTYPE(state) gpu::interconnect::maxwell3d::state::EngineRegisters

    static gpu::interconnect::maxwell3d::PipelineState::EngineRegisters MakePipelineStateRegisters(const Maxwell3D::Registers &registers) {
        return {
            .pipelineStageRegisters = util::MergeInto<REGTYPE(PipelineStageState), type::PipelineCount>(*registers.pipelines, *registers.programRegion),
            .colorRenderTargetsRegisters = util::MergeInto<REGTYPE(ColorRenderTargetState), type::ColorTargetCount>(*registers.colorTargets, *registers.surfaceClip),
            .depthRenderTargetRegisters = {*registers.ztSize, *registers.ztOffset, *registers.ztFormat, *registers.ztBlockSize, *registers.ztArrayPitch, *registers.ztSelect, *registers.ztLayer, *registers.surfaceClip},
            .vertexInputRegisters = {*registers.vertexStreams, *registers.vertexStreamInstance, *registers.vertexAttributes},
            .inputAssemblyRegisters = {*registers.primitiveRestartEnable},
            .tessellationRegisters = {*registers.patchSize, *registers.tessellationParameters},
            .rasterizationRegisters = {*registers.rasterEnable, *registers.frontPolygonMode, *registers.backPolygonMode, *registers.oglCullEnable, *registers.oglCullFace, *registers.windowOrigin, *registers.oglFrontFace, *registers.viewportClipControl, *registers.polyOffset, *registers.provokingVertex, *registers.pointSize, *registers.zClipRange},
            .depthStencilRegisters = {*registers.depthTestEnable, *registers.depthWriteEnable, *registers.depthFunc, *registers.depthBoundsTestEnable, *registers.stencilTestEnable, *registers.twoSidedStencilTestEnable, *registers.stencilOps, *registers.stencilBack, *registers.alphaTestEnable, *registers.alphaFunc, *registers.alphaRef},
            .colorBlendRegisters = {*registers.logicOp, *registers.singleCtWriteControl, *registers.ctWrites, *registers.blendStatePerTargetEnable, *registers.blendPerTargets, *registers.blend},
            .transformFeedbackRegisters = {*registers.streamOutputEnable, *registers.streamOutControls, *registers.streamOutLayoutSelect},
            .globalShaderConfigRegisters = {*registers.postVtgShaderAttributeSkipMask, *registers.bindlessTexture, *registers.apiMandatedEarlyZEnable, *registers.viewportScaleOffsetEnable},
            .ctSelect = *registers.ctSelect
        };
    }

    static gpu::interconnect::maxwell3d::ActiveState::EngineRegisters MakeActiveStateRegisters(const Maxwell3D::Registers &registers) {
        return {
            .pipelineRegisters = MakePipelineStateRegisters(registers),
            .vertexBuffersRegisters = util::MergeInto<REGTYPE(VertexBufferState), type::VertexStreamCount>(*registers.vertexStreams, *registers.vertexStreamLimits),
            .indexBufferRegisters = {*registers.indexBuffer},
            .transformFeedbackBuffersRegisters = util::MergeInto<REGTYPE(TransformFeedbackBufferState), type::StreamOutBufferCount>(*registers.streamOutBuffers, *registers.streamOutputEnable),
            .viewportsRegisters = util::MergeInto<REGTYPE(ViewportState), type::ViewportCount>(registers.viewports[0], registers.viewportClips[0], *registers.viewports, *registers.viewportClips, *registers.windowOrigin, *registers.viewportScaleOffsetEnable, *registers.surfaceClip),
            .scissorsRegisters = util::MergeInto<REGTYPE(ScissorState), type::ViewportCount>(*registers.scissors),
            .lineWidthRegisters = {*registers.lineWidth, *registers.lineWidthAliased, *registers.aliasedLineWidthEnable},
            .depthBiasRegisters = {*registers.depthBias, *registers.depthBiasClamp, *registers.slopeScaleDepthBias},
            .blendConstantsRegisters = {*registers.blendConsts},
            .depthBoundsRegisters = {*registers.depthBoundsMin, *registers.depthBoundsMax},
            .stencilValuesRegisters = {*registers.stencilValues, *registers.backStencilValues, *registers.twoSidedStencilTestEnable},
        };
    }

    static gpu::interconnect::maxwell3d::Maxwell3D::EngineRegisterBundle MakeEngineRegisters(const Maxwell3D::Registers &registers) {
        return {
            .activeStateRegisters = MakeActiveStateRegisters(registers),
            .clearRegisters = {registers.scissors[0], registers.viewportClips[0], *registers.clearRect, *registers.colorClearValue, *registers.zClearValue, *registers.stencilClearValue, *registers.surfaceClip, *registers.clearSurfaceControl},
            .constantBufferSelectorRegisters = {*registers.constantBufferSelector},
            .samplerPoolRegisters = {*registers.texSamplerPool, *registers.texHeaderPool},
            .samplerBinding = *registers.samplerBinding,
            .texturePoolRegisters = {*registers.texHeaderPool}
        };
    }
    #undef REGTYPE

    type::DrawTopology Maxwell3D::ApplyTopologyOverride(type::DrawTopology beginMethodTopology) {
        return registers.primitiveTopologyControl->override == type::PrimitiveTopologyControl::Override::UseTopologyInBeginMethods ?
               beginMethodTopology : type::ConvertPrimitiveTopologyToDrawTopology(*registers.primitiveTopology);
    }

    bool Maxwell3D::CheckRenderEnable() {
        if (registers.renderEnableOverride->mode == Registers::RenderEnableOverride::Mode::AlwaysRender)
            return true;
        else if (registers.renderEnableOverride->mode == Registers::RenderEnableOverride::Mode::NeverRender)
            return false;



        switch (registers.renderEnable->mode) {
            case Registers::RenderEnable::Mode::True:
                return true;
            case Registers::RenderEnable::Mode::False:
                return false;
            case Registers::RenderEnable::Mode::Conditional:
                // TODO: Use indirect draws to emulate conditional rendering with queries, for now just ignore such cases as they would decrease performance anyway by forcing a CPU sync
                if (interconnect.QueryPresentAtAddress(u64{registers.renderEnable->offset}))
                    return true;

                return channelCtx.asCtx->gmmu.Read<u32>(registers.renderEnable->offset) != 0;
            case Registers::RenderEnable::Mode::RenderIfEqual:
                // TODO: See above
                if (interconnect.QueryPresentAtAddress(u64{registers.renderEnable->offset}) ||
                    interconnect.QueryPresentAtAddress(u64{registers.renderEnable->offset + 16}))
                    return true;

                return channelCtx.asCtx->gmmu.Read<u32>(registers.renderEnable->offset) ==
                    channelCtx.asCtx->gmmu.Read<u32>(registers.renderEnable->offset + 16);
            case Registers::RenderEnable::Mode::RenderIfNotEqual:
                // TODO: See above
                if (interconnect.QueryPresentAtAddress(u64{registers.renderEnable->offset}) ||
                    interconnect.QueryPresentAtAddress(u64{registers.renderEnable->offset + 16}))
                    return true;

                return channelCtx.asCtx->gmmu.Read<u32>(registers.renderEnable->offset) !=
                    channelCtx.asCtx->gmmu.Read<u32>(registers.renderEnable->offset + 16);
        }

        return true;
    }


    Maxwell3D::Maxwell3D(const DeviceState &state, ChannelContext &channelCtx, MacroState &macroState)
        : MacroEngineBase{macroState},
          syncpoints{state.soc->host1x.syncpoints},
          i2m{state, channelCtx},
          dirtyManager{registers},
          interconnect{*state.gpu, channelCtx, state.process->trap, state.process->memory, dirtyManager, MakeEngineRegisters(registers)},
          channelCtx{channelCtx} {
        channelCtx.executor.AddFlushCallback([this]() { FlushEngineState(); });
        InitializeRegisters();
    }

    void Maxwell3D::DrawHostIndices(span<u32> indices) {
        if (indices.empty())
            return;

        if (CheckRenderEnable())
            interconnect.Draw(ApplyTopologyOverride(registers.begin->op), *registers.streamOutputEnable, true, static_cast<u32>(indices.size()), 0, 1,
                              *registers.globalBaseVertexIndex, *registers.globalBaseInstanceIndex, indices);
    }

    __attribute__((always_inline)) void Maxwell3D::FlushDeferredDraw() {
        if (batchEnableState.drawActive) {
            batchEnableState.drawActive = false;
            if (CheckRenderEnable())
                interconnect.Draw(deferredDraw.drawTopology, *registers.streamOutputEnable, deferredDraw.indexed, deferredDraw.drawCount, deferredDraw.drawFirst, deferredDraw.instanceCount, deferredDraw.drawBaseVertex, deferredDraw.drawBaseInstance);
            deferredDraw.instanceCount = 1;
        }
    }

    __attribute__((always_inline)) void Maxwell3D::HandleMethod(u32 method, u32 argument) {
        if (method == ENGINE_STRUCT_OFFSET(mme, shadowRamControl)) [[unlikely]] {
            shadowRegisters.raw[method] = registers.raw[method] = argument;
            return;
        }

        if (shadowRegisters.mme->shadowRamControl == type::MmeShadowRamControl::MethodTrack || shadowRegisters.mme->shadowRamControl == type::MmeShadowRamControl::MethodTrackWithFilter) [[unlikely]]
            shadowRegisters.raw[method] = argument;
        else if (shadowRegisters.mme->shadowRamControl == type::MmeShadowRamControl::MethodReplay) [[unlikely]]
            argument = shadowRegisters.raw[method];


        bool redundant{registers.raw[method] == argument};
        u32 origRegisterValue{registers.raw[method]};
        registers.raw[method] = argument;

        if (batchEnableState.raw) {
            if (batchEnableState.constantBufferActive) {
                switch (method) {
                    // Add to the batch constant buffer update buffer
                    // Return early here so that any code below can rely on the fact that any cbuf updates will always be the first of a batch
                    #define LOAD_CONSTANT_BUFFER_CALLBACKS(z, index, data_)                \
                    ENGINE_STRUCT_ARRAY_CASE(loadConstantBuffer, data, index, { \
                        batchLoadConstantBuffer.buffer.push_back(argument);         \
                        registers.loadConstantBuffer->offset += 4;              \
                        return;                                                   \
                    })

                    BOOST_PP_REPEAT(16, LOAD_CONSTANT_BUFFER_CALLBACKS, 0)
                    #undef LOAD_CONSTANT_BUFFER_CALLBACKS
                    default:
                        // When a method other than constant buffer update is called submit our submit the previously built-up update as a batch
                        registers.raw[method] = origRegisterValue;
                        interconnect.DisableQuickConstantBufferBind();
                        interconnect.LoadConstantBuffer(batchLoadConstantBuffer.buffer, batchLoadConstantBuffer.startOffset);
                        batchEnableState.constantBufferActive = false;
                        batchLoadConstantBuffer.Reset();
                        registers.raw[method] = argument;
                        break; // Continue on here to handle the actual method
                }
            } else if (batchEnableState.drawActive) { // See DeferredDrawState comment for full details
                switch (method) {
                    ENGINE_CASE(begin, {
                        if (begin.instanceId == Registers::Begin::InstanceId::Subsequent) {
                            if (deferredDraw.drawTopology != begin.op &&
                                registers.primitiveTopologyControl->override == type::PrimitiveTopologyControl::Override::UseTopologyInBeginMethods)
                                LOGW("Vertex topology changed partway through instanced draw!");

                            deferredDraw.instanceCount++;
                        } else {
                            FlushDeferredDraw();
                            break; // This instanced draw is finished, continue on to handle the next draw
                        }

                        return;
                    })

                    // Can be ignored since we handle drawing in draw{Vertex,Index}Count
                    ENGINE_CASE(end, { return; })

                    // Draws here can be ignored since they're just repeats of the original instanced draw
                    ENGINE_CASE(drawVertexArray, {
                        if (!redundant)
                            LOGW("Vertex count changed partway through instanced draw!");
                        return;
                    })
                    ENGINE_CASE(drawIndexBuffer, {
                        if (!redundant)
                            LOGW("Index count changed partway through instanced draw!");
                        return;
                    })

                    ENGINE_CASE(drawVertexArrayBeginEndInstanceSubsequent, {
                        deferredDraw.instanceCount++;
                        return;
                    })

                    ENGINE_CASE(drawIndexBuffer32BeginEndInstanceSubsequent, {
                        deferredDraw.instanceCount++;
                        return;
                    })

                    ENGINE_CASE(drawIndexBuffer16BeginEndInstanceSubsequent, {
                        deferredDraw.instanceCount++;
                        return;
                    })

                    ENGINE_CASE(drawIndexBuffer8BeginEndInstanceSubsequent, {
                        deferredDraw.instanceCount++;
                        return;
                    })

                    // Once we stop calling draw methods flush the current draw since drawing is dependent on the register state not changing
                    default:
                        registers.raw[method] = origRegisterValue;
                        FlushDeferredDraw();
                        registers.raw[method] = argument;
                        break;
                }
            }
        }

        if (!redundant)
            dirtyManager.MarkDirty(method);

        switch (method) {
            ENGINE_STRUCT_CASE(mme, instructionRamLoad, {
                if (registers.mme->instructionRamPointer >= macroState.macroCode.size())
                    throw exception("Macro memory is full!");

                macroState.macroCode[registers.mme->instructionRamPointer++] = instructionRamLoad;
                macroState.Invalidate();

                // Wraparound writes
                // This works on HW but will also generate an error interrupt
                registers.mme->instructionRamPointer %= macroState.macroCode.size();
            })

            ENGINE_STRUCT_CASE(mme, startAddressRamLoad, {
                if (registers.mme->startAddressRamPointer >= macroState.macroPositions.size())
                    throw exception("Maximum amount of macros reached!");

                macroState.macroPositions[registers.mme->startAddressRamPointer++] = startAddressRamLoad;
                macroState.Invalidate();
            })

            ENGINE_STRUCT_CASE(i2m, launchDma, {
                FlushEngineState();
                i2m.LaunchDma(*registers.i2m);
            })

            ENGINE_STRUCT_CASE(i2m, loadInlineData, {
                i2m.LoadInlineData(*registers.i2m, loadInlineData);
            })

            ENGINE_CASE(clearReportValue, {
                interconnect.ResetCounter(clearReportValue.type);
            })

            ENGINE_CASE(syncpointAction, {
                LOGD("Increment syncpoint: {}", static_cast<u16>(syncpointAction.id));
                channelCtx.executor.AddDeferredAction([=, syncpoints = &this->syncpoints, index = syncpointAction.id]() {
                    syncpoints->at(index).host.Increment();
                });
                syncpoints.at(syncpointAction.id).guest.Increment();
            })

            ENGINE_CASE(clearSurface, {
                if (CheckRenderEnable())
                    interconnect.Clear(clearSurface);
            })

            ENGINE_CASE(begin, {
                // If we reach here then we aren't in a deferred draw so theres no need to flush anything
                if (begin.instanceId == Registers::Begin::InstanceId::Subsequent)
                    deferredDraw.instanceCount++;
                else
                    deferredDraw.instanceCount = 1;
            })

            ENGINE_STRUCT_CASE(drawVertexArray, count, {
                // Defer the draw until the first non-draw operation to allow for detecting instanced draws (see DeferredDrawState comment)
                deferredDraw.Set(count, *registers.vertexArrayStart, 0,
                                 *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(registers.begin->op),
                                 false);
                batchEnableState.drawActive = true;
            })

            ENGINE_CASE(drawVertexArrayBeginEndInstanceFirst, {
                deferredDraw.Set(drawVertexArrayBeginEndInstanceFirst.count, drawVertexArrayBeginEndInstanceFirst.startIndex, 0,
                                 *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(drawVertexArrayBeginEndInstanceFirst.topology),
                                 false);
                deferredDraw.instanceCount = 1;
                batchEnableState.drawActive = true;
            })

            ENGINE_CASE(drawVertexArrayBeginEndInstanceSubsequent, {
                deferredDraw.Set(drawVertexArrayBeginEndInstanceSubsequent.count, drawVertexArrayBeginEndInstanceSubsequent.startIndex, 0,
                                 *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(drawVertexArrayBeginEndInstanceSubsequent.topology),
                                 false);
                deferredDraw.instanceCount++;
                batchEnableState.drawActive = true;
            })

            // Inline index draws: the indices are sent through the method stream between `begin` and `end`, we gather them as 32-bit indices and draw on `end`
            // NOTE: The inlineIndex*Align registers (start offset/count of valid indices) are currently ignored, which matches the behaviour of other emulators
            ENGINE_CASE(drawInlineIndex4X8, {
                inlineIndices.insert(inlineIndices.end(), {drawInlineIndex4X8.index0, drawInlineIndex4X8.index1, drawInlineIndex4X8.index2, drawInlineIndex4X8.index3});
            })

            ENGINE_CASE(drawInlineIndex2X16, {
                inlineIndices.insert(inlineIndices.end(), {drawInlineIndex2X16.even, drawInlineIndex2X16.odd});
            })

            ENGINE_CASE(drawInlineIndex, {
                inlineIndices.push_back(drawInlineIndex);
            })

            ENGINE_CASE(end, {
                if (!inlineIndices.empty()) {
                    DrawHostIndices(span<u32>(inlineIndices));
                    inlineIndices.clear();
                }
            })

            ENGINE_STRUCT_CASE(drawZeroIndex, count, {
                constexpr u32 MaxZeroIndexCount{1U << 24}; // Sanity limit to avoid huge host allocations from corrupt command streams
                if (count > MaxZeroIndexCount) {
                    LOGW("drawZeroIndex count 0x{:X} exceeds the supported maximum, skipping draw", count);
                } else {
                    std::vector<u32> zeroIndices(count, 0);
                    DrawHostIndices(span<u32>(zeroIndices));
                }
            })

            ENGINE_STRUCT_CASE(drawAuto, byteCount, {
                // Needs transform feedback counter buffers (vkCmdDrawIndirectByteCountEXT) which aren't emulated yet, skip the draw instead of crashing
                static bool warned{};
                if (!warned) {
                    LOGW("drawAuto is unimplemented (requires transform feedback counters), draws using it are skipped");
                    warned = true;
                }
            })

            ENGINE_STRUCT_CASE(drawIndexBuffer, count, {
                // Defer the draw until the first non-draw operation to allow for detecting instanced draws (see DeferredDrawState comment)
                deferredDraw.Set(count, registers.indexBuffer->first,
                                 *registers.globalBaseVertexIndex, *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(registers.begin->op),
                                 true);
                batchEnableState.drawActive = true;
            })

            ENGINE_CASE(drawIndexBuffer32BeginEndInstanceFirst, {
                deferredDraw.Set(drawIndexBuffer32BeginEndInstanceFirst.count, drawIndexBuffer32BeginEndInstanceFirst.first,
                                 *registers.globalBaseVertexIndex, *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(drawIndexBuffer32BeginEndInstanceFirst.topology),
                                 true);
                deferredDraw.instanceCount = 1;
                batchEnableState.drawActive = true;
            })

            ENGINE_CASE(drawIndexBuffer16BeginEndInstanceFirst, {
                deferredDraw.Set(drawIndexBuffer16BeginEndInstanceFirst.count, drawIndexBuffer16BeginEndInstanceFirst.first,
                                 *registers.globalBaseVertexIndex, *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(drawIndexBuffer16BeginEndInstanceFirst.topology),
                                 true);
                deferredDraw.instanceCount = 1;
                batchEnableState.drawActive = true;
            })

            ENGINE_CASE(drawIndexBuffer8BeginEndInstanceFirst, {
                deferredDraw.Set(drawIndexBuffer8BeginEndInstanceFirst.count, drawIndexBuffer8BeginEndInstanceFirst.first,
                                 *registers.globalBaseVertexIndex, *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(drawIndexBuffer8BeginEndInstanceFirst.topology),
                                 true);
                deferredDraw.instanceCount = 1;
                batchEnableState.drawActive = true;
            })

            ENGINE_CASE(drawIndexBuffer32BeginEndInstanceSubsequent, {
                deferredDraw.Set(drawIndexBuffer32BeginEndInstanceSubsequent.count, drawIndexBuffer32BeginEndInstanceSubsequent.first,
                                 *registers.globalBaseVertexIndex, *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(drawIndexBuffer32BeginEndInstanceSubsequent.topology),
                                 true);
                deferredDraw.instanceCount++;
                batchEnableState.drawActive = true;
            })

            ENGINE_CASE(drawIndexBuffer16BeginEndInstanceSubsequent, {
                deferredDraw.Set(drawIndexBuffer16BeginEndInstanceSubsequent.count, drawIndexBuffer16BeginEndInstanceSubsequent.first,
                                 *registers.globalBaseVertexIndex, *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(drawIndexBuffer16BeginEndInstanceSubsequent.topology),
                                 true);
                deferredDraw.instanceCount++;
                batchEnableState.drawActive = true;
            })

            ENGINE_CASE(drawIndexBuffer8BeginEndInstanceSubsequent, {
                deferredDraw.Set(drawIndexBuffer8BeginEndInstanceSubsequent.count, drawIndexBuffer8BeginEndInstanceSubsequent.first,
                                 *registers.globalBaseVertexIndex, *registers.globalBaseInstanceIndex,
                                 ApplyTopologyOverride(drawIndexBuffer8BeginEndInstanceSubsequent.topology),
                                 true);
                deferredDraw.instanceCount++;
                batchEnableState.drawActive = true;
            })

            ENGINE_STRUCT_CASE(semaphore, info, {
                if (info.reductionEnable)
                    LOGW("Semaphore reduction is unimplemented!");

                switch (info.op) {
                    case type::SemaphoreInfo::Op::Release:
                        channelCtx.executor.AddDeferredAction([=, this, semaphore = *registers.semaphore]() {
                            WriteSemaphoreResult(semaphore, semaphore.payload);
                        });
                        break;

                    case type::SemaphoreInfo::Op::Counter: {
                        switch (info.counterType) {
                            case type::SemaphoreInfo::CounterType::Zero:
                                channelCtx.executor.AddDeferredAction([=, this, semaphore = *registers.semaphore]() {
                                    WriteSemaphoreResult(semaphore, semaphore.payload);
                                });
                                break;
                            case type::SemaphoreInfo::CounterType::SamplesPassed:
                                // Return a fake result for now
                                interconnect.Query({registers.semaphore->address}, info.counterType,
                                                   registers.semaphore->info.structureSize == type::SemaphoreInfo::StructureSize::FourWords ?
                                                   GetGpuTimeTicks() : std::optional<u64>{});
                                break;

                            default:
                                LOGD("Unsupported semaphore counter type: 0x{:X}", static_cast<u8>(info.counterType));
                                break;
                        }
                        break;
                    }

                    default:
                        LOGW("Unsupported semaphore operation: 0x{:X}", static_cast<u8>(info.op));
                        break;
                }
            })

            ENGINE_ARRAY_CASE(firmwareCall, 4, {
                registers.raw[0xD00] = 1;
            })

            ENGINE_CASE(invalidateSamplerCacheAll, {
                channelCtx.executor.Submit();
            })

            ENGINE_CASE(invalidateTextureHeaderCacheAll, {
                channelCtx.executor.Submit();
            })

            // Begin a batch constant buffer update, this case will never be reached if a batch update is currently active
            #define LOAD_CONSTANT_BUFFER_CALLBACKS(z, index, data_)                                      \
            ENGINE_STRUCT_ARRAY_CASE(loadConstantBuffer, data, index, {                       \
                batchLoadConstantBuffer.startOffset = registers.loadConstantBuffer->offset;              \
                batchLoadConstantBuffer.buffer.push_back(data);                                          \
                batchEnableState.constantBufferActive = true;                                        \
                registers.loadConstantBuffer->offset += 4;                                    \
            })

            BOOST_PP_REPEAT(16, LOAD_CONSTANT_BUFFER_CALLBACKS, 0)
            #undef LOAD_CONSTANT_BUFFER_CALLBACKS

            #define PIPELINE_CALLBACKS(z, idx, data)                                                                                         \
                ENGINE_ARRAY_STRUCT_CASE(bindGroups, idx, constantBuffer, {                                                                  \
                    interconnect.BindConstantBuffer(static_cast<type::ShaderStage>(idx), constantBuffer.shaderSlot, constantBuffer.valid);   \
                })

            BOOST_PP_REPEAT(5, PIPELINE_CALLBACKS, 0)
            static_assert(type::ShaderStageCount == 5 && type::ShaderStageCount < BOOST_PP_LIMIT_REPEAT);
            #undef PIPELINE_CALLBACKS
            default:
                break;
        }
    }

    void Maxwell3D::WriteSemaphoreResult(const Registers::Semaphore &semaphore, u64 result) {
        switch (semaphore.info.structureSize) {
            case type::SemaphoreInfo::StructureSize::OneWord:
                channelCtx.asCtx->gmmu.Write(semaphore.address, static_cast<u32>(result));
                LOGD("address: 0x{:X} payload: {}", semaphore.address, result);
                break;

            case type::SemaphoreInfo::StructureSize::FourWords: {
                // Write timestamp first to ensure correct ordering
                u64 timestamp{GetGpuTimeTicks()};
                channelCtx.asCtx->gmmu.Write(semaphore.address + 8, timestamp);
                channelCtx.asCtx->gmmu.Write(semaphore.address, result);
                LOGD("address: 0x{:X} payload: {} timestamp: {}", semaphore.address, result, timestamp);

                break;
            }
        }
    }

    void Maxwell3D::FlushEngineState() {
        FlushDeferredDraw();

        if (batchEnableState.constantBufferActive) {
            interconnect.LoadConstantBuffer(batchLoadConstantBuffer.buffer, batchLoadConstantBuffer.startOffset);
            batchEnableState.constantBufferActive = false;
            batchLoadConstantBuffer.Reset();
        }

        interconnect.DisableQuickConstantBufferBind();
    }

    __attribute__((always_inline)) void Maxwell3D::CallMethod(u32 method, u32 argument) {
        LOGV("Called method in Maxwell 3D: 0x{:X} args: 0x{:X}", method, argument);

        HandleMethod(method, argument);
    }

    void Maxwell3D::CallMethodBatchNonInc(u32 method, span<u32> arguments) {
        switch (method) {
            case ENGINE_STRUCT_OFFSET(i2m, loadInlineData):
                i2m.LoadInlineData(*registers.i2m, arguments);
                return;
            default:
                break;
        }

        for (u32 argument : arguments)
            HandleMethod(method, argument);
    }

    void Maxwell3D::CallMethodFromMacro(u32 method, u32 argument) {
        HandleMethod(method, argument);
    }

    u32 Maxwell3D::ReadMethodFromMacro(u32 method) {
        return registers.raw[method];
    }

    void Maxwell3D::DrawInstanced(u32 drawTopology, u32 vertexArrayCount, u32 instanceCount, u32 vertexArrayStart, u32 globalBaseInstanceIndex) {
        FlushEngineState();

        auto topology{static_cast<type::DrawTopology>(drawTopology)};
        registers.globalBaseInstanceIndex = globalBaseInstanceIndex;
        registers.vertexArrayStart = vertexArrayStart;
        if (CheckRenderEnable())
            interconnect.Draw(topology, *registers.streamOutputEnable, false, vertexArrayCount, vertexArrayStart, instanceCount, 0, globalBaseInstanceIndex);
        registers.globalBaseInstanceIndex = 0;
    }

    void Maxwell3D::DrawIndexedInstanced(u32 drawTopology, u32 indexBufferCount, u32 instanceCount, u32 globalBaseVertexIndex, u32 indexBufferFirst, u32 globalBaseInstanceIndex) {
        FlushEngineState();
        auto topology{static_cast<type::DrawTopology>(drawTopology)};
        if (CheckRenderEnable())
            interconnect.Draw(topology, *registers.streamOutputEnable, true, indexBufferCount, indexBufferFirst, instanceCount, globalBaseVertexIndex, globalBaseInstanceIndex);
    }

    void Maxwell3D::DrawIndexedIndirect(u32 drawTopology, span<u8> indirectBuffer, u32 count, u32 stride) {
        FlushEngineState();
        auto topology{static_cast<type::DrawTopology>(drawTopology)};
        if (CheckRenderEnable())
            interconnect.DrawIndirect(topology, *registers.streamOutputEnable, true, indirectBuffer, count, stride);
    }

}
