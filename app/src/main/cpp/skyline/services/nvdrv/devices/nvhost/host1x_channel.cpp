// SPDX-License-Identifier: MIT OR MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <atomic>
#include <soc.h>
#include <services/nvdrv/devices/deserialisation/deserialisation.h>
#include "host1x_channel.h"

namespace skyline::service::nvdrv::device::nvhost {
    /**
     * @brief Writes what a command buffer does (the Host1x opcodes it has) to the log, used to see what the games send to the video decoder while it isn't implemented
     * @note Only the first few submissions of every channel are described, the rest would flood the log
     */
    static void DescribeCommandBuffer(core::ChannelType channelType, span<u32> gather) {
        static std::array<std::atomic<u32>, 8> submissionCounts{};
        u32 channelIndex{static_cast<u32>(channelType)};
        if (channelIndex >= submissionCounts.size() || submissionCounts[channelIndex].fetch_add(1) >= 8)
            return;

        LOGI("Command buffer for channel {}: {} words", channelIndex, gather.size());

        u32 lines{};
        for (size_t i{}; i < gather.size() && lines < 48; i++, lines++) {
            u32 word{gather[i]};
            u32 opcode{word >> 28};
            u32 offset{(word >> 16) & 0xFFF};
            switch (opcode) {
                case 0: // SetClass
                    LOGI("  setclass: class 0x{:X}, offset 0x{:X}, mask 0x{:X}", (word >> 6) & 0x3FF, offset, word & 0x3F);
                    for (u32 bit{}; bit < 6 && i + 1 < gather.size(); bit++)
                        if (word & (1u << bit))
                            LOGI("    reg 0x{:X} <- 0x{:X}", offset + bit, gather[++i]);
                    break;
                case 1: // Increment
                case 2: // Non-increment
                    LOGI("  {}: offset 0x{:X}, {} words", opcode == 1 ? "incr" : "nonincr", offset, word & 0xFFFF);
                    for (u32 j{}; j < (word & 0xFFFF) && i + 1 < gather.size() && lines < 48; j++, lines++)
                        LOGI("    reg 0x{:X} <- 0x{:X}", opcode == 1 ? offset + j : offset, gather[++i]);
                    break;
                case 3: // Mask
                    LOGI("  mask: offset 0x{:X}, mask 0x{:X}", offset, word & 0xFFFF);
                    for (u32 bit{}; bit < 16 && i + 1 < gather.size(); bit++)
                        if (word & (1u << bit))
                            LOGI("    reg 0x{:X} <- 0x{:X}", offset + bit, gather[++i]);
                    break;
                case 4: // Immediate
                    LOGI("  imm: reg 0x{:X} <- 0x{:X}", offset, word & 0xFFFF);
                    break;
                default:
                    LOGI("  unhandled opcode {} (word 0x{:X})", opcode, word);
                    return;
            }
        }
    }

    Host1xChannel::Host1xChannel(const DeviceState &state,
                                 Driver &driver,
                                 Core &core,
                                 const SessionContext &ctx,
                                 core::ChannelType channelType)
        : NvDevice(state, driver, core, ctx),
          channelType(channelType) {
        state.soc->host1x.channels[static_cast<size_t>(channelType)].Start();
    }

    PosixResult Host1xChannel::SetNvmapFd(In<FileDescriptor> fd) {
        LOGD("fd: {}", fd);
        return PosixResult::Success;
    }

    PosixResult Host1xChannel::Submit(span<SubmitCmdBuf> cmdBufs,
                                      span<SubmitReloc> relocs, span<u32> relocShifts,
                                      span<SubmitSyncpointIncr> syncpointIncrs, span<u32> fenceThresholds) {
        LOGD("numCmdBufs: {}, numRelocs: {}, numSyncpointIncrs: {}, numFenceThresholds: {}",
                            cmdBufs.size(), relocs.size(), syncpointIncrs.size(), fenceThresholds.size());

        if (fenceThresholds.size() > syncpointIncrs.size())
            return PosixResult::InvalidArgument;

        std::scoped_lock lock(channelMutex);

        // A relocation replaces a word of a command buffer (the address of a buffer) with the address the buffer was pinned at, shifted as the engine expects. Games that use the video decoder always send them, they used to make the emulator throw
        for (size_t i{}; i < relocs.size(); i++) {
            const auto &reloc{relocs[i]};
            auto patchHandle{core.nvMap.GetHandle(reloc.patchMem)};
            auto pinHandle{core.nvMap.GetHandle(reloc.pinMem)};
            if (!patchHandle || !pinHandle || reloc.patchOffset + sizeof(u32) > patchHandle->size) {
                LOGW("Skipping an invalid relocation: patch handle {} offset 0x{:X}, pin handle {}", reloc.patchMem, reloc.patchOffset, reloc.pinMem);
                continue;
            }
            if (!pinHandle->pinVirtAddress)
                LOGW("Relocation to a handle that wasn't pinned: {}", reloc.pinMem);

            u32 shift{i < relocShifts.size() ? relocShifts[i] : 0};
            u32 value{static_cast<u32>((static_cast<u64>(pinHandle->pinVirtAddress) + reloc.pinOffset) >> shift)};
            *reinterpret_cast<u32 *>(patchHandle->address + reloc.patchOffset) = value;
            LOGD("Relocation {}: handle {} + 0x{:X} -> word at 0x{:X} of handle {} = 0x{:X}", i, reloc.pinMem, reloc.pinOffset, reloc.patchOffset, reloc.patchMem, value);
        }

        for (size_t i{}; i < syncpointIncrs.size(); i++) {
            const auto &incr{syncpointIncrs[i]};

            u32 max{core.syncpointManager.IncrementSyncpointMaxExt(incr.syncpointId, incr.numIncrs)};

            // Increment syncpoints on the CPU to avoid needing to pass through the emulated nvdec code which currently does nothing
            for (size_t j{}; j < incr.numIncrs; j++)
                state.soc->host1x.syncpoints[incr.syncpointId].Increment();

            if (i < fenceThresholds.size())
                fenceThresholds[i] = max;
        }

        for (const auto &cmdBuf : cmdBufs) {
            auto handleDesc{core.nvMap.GetHandle(cmdBuf.mem)};
            if (!handleDesc)
                throw exception("Invalid handle passed for a command buffer!");

            u64 gatherAddress{handleDesc->address + cmdBuf.offset};
            LOGD("Submit gather, CPU address: 0x{:X}, words: 0x{:X}", gatherAddress, cmdBuf.words);

            span gather(reinterpret_cast<u32 *>(gatherAddress), cmdBuf.words);

            // The video decoder isn't implemented yet, what the game sends to it is only written to the log
            if (channelType == core::ChannelType::NvDec || channelType == core::ChannelType::VIC)
                DescribeCommandBuffer(channelType, gather);

            // Skip submitting the cmdbufs as no functionality is implemented
            // state.soc->host1x.channels[static_cast<size_t>(channelType)].Push(gather);
        }

        return PosixResult::Success;
    }

    PosixResult Host1xChannel::GetSyncpoint(In<u32> channelSyncpointIdx, Out<u32> syncpointId) {
        LOGD("channelSyncpointIdx: {}", channelSyncpointIdx);

        if (channelSyncpointIdx > 0)
            throw exception("Multiple channel syncpoints are unimplemented!");

        u32 id{core::SyncpointManager::ChannelSyncpoints[static_cast<u32>(channelType)]};
        if (!id)
            throw exception("Requested syncpoint for a channel with none specified!");

        LOGD("syncpointId: {}", id);
        syncpointId = id;
        return PosixResult::Success;
    }

    PosixResult Host1xChannel::GetWaitBase(In<core::ChannelType> pChannelType, Out<u32> waitBase) {
        LOGD("channelType: {}", static_cast<u32>(pChannelType));
        waitBase = 0;
        return PosixResult::Success;
    }

    PosixResult Host1xChannel::SetSubmitTimeout(In<u32> timeout) {
        LOGD("timeout: {}", timeout);
        return PosixResult::Success;
    }

    PosixResult Host1xChannel::MapBuffer(u8 compressed, span<BufferHandle> handles) {
        LOGD("compressed: {}", compressed);

        for (auto &bufferHandle : handles) {
            bufferHandle.address = core.nvMap.PinHandle(bufferHandle.handle);
            LOGD("handle: {}, address: 0x{:X}", bufferHandle.handle, bufferHandle.address);
        }

        return PosixResult::Success;
    }

    PosixResult Host1xChannel::UnmapBuffer(u8 compressed, span<BufferHandle> handles) {
        LOGD("compressed: {}", compressed);

        for (auto &bufferHandle : handles) {
            core.nvMap.UnpinHandle(bufferHandle.handle);
            LOGD("handle: {}", bufferHandle.handle);
        }

        return PosixResult::Success;
    }

// @fmt:off
#include <services/nvdrv/devices/deserialisation/macro_def.inc>
    static constexpr u32 Host1xChannelMagic{0x00};
    static constexpr u32 GpuChannelMagic{0x48}; //!< Used for SetNvmapFd which is needed in both GPU and host1x channels

    VARIABLE_IOCTL_HANDLER_FUNC(Host1xChannel,
        IOCTL_CASE_ARGS(IN,    SIZE(0x4), MAGIC(GpuChannelMagic),    FUNC(0x1),
                        SetNvmapFd,       ARGS(In<FileDescriptor>))
        IOCTL_CASE_ARGS(INOUT, SIZE(0x8), MAGIC(Host1xChannelMagic), FUNC(0x2),
                        GetSyncpoint,     ARGS(In<u32>, Out<u32>))
        IOCTL_CASE_ARGS(INOUT, SIZE(0x8), MAGIC(Host1xChannelMagic), FUNC(0x3),
                        GetWaitBase,      ARGS(In<core::ChannelType>, Out<u32>))
        IOCTL_CASE_ARGS(IN,    SIZE(0x4), MAGIC(Host1xChannelMagic), FUNC(0x7),
                        SetSubmitTimeout, ARGS(In<u32>))
    ,
        VARIABLE_IOCTL_CASE_ARGS(INOUT, MAGIC(Host1xChannelMagic), FUNC(0x1),
                                 Submit,      ARGS(Save<u32, 0>, Save<u32, 1>, Save<u32, 2>, Save<u32, 3>,
                                                   SlotSizeSpan<SubmitCmdBuf, 0>,
                                                   SlotSizeSpan<SubmitReloc, 1>, SlotSizeSpan<u32, 1>,
                                                   SlotSizeSpan<SubmitSyncpointIncr, 2>, SlotSizeSpan<u32, 3>))
        VARIABLE_IOCTL_CASE_ARGS(INOUT, MAGIC(Host1xChannelMagic), FUNC(0x9),
                                 MapBuffer,   ARGS(Save<u32, 0>, Pad<u32>, In<u8>, Pad<u8, 3>, SlotSizeSpan<BufferHandle, 0>))
        VARIABLE_IOCTL_CASE_ARGS(INOUT, MAGIC(Host1xChannelMagic), FUNC(0xA),
                                 UnmapBuffer, ARGS(Save<u32, 0>, Pad<u32>, In<u8>, Pad<u8, 3>, SlotSizeSpan<BufferHandle, 0>))
    )
#include <services/nvdrv/devices/deserialisation/macro_undef.inc>
// @fmt:on
}
