// SPDX-License-Identifier: MPL-2.0

#include "ISecureProcessor.h"

#include <common.h>
#include <kernel/results.h>

namespace skyline::service::spl {
    ISecureProcessor::ISecureProcessor(const DeviceState &state, ServiceManager &manager) : BaseService(state, manager) {}

    Result ISecureProcessor::GetConfig(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        enum class ConfigItem : u32 {
            DisableProgramVerification = 1,
            DramId = 2,
            SecurityEngineInterruptNumber = 3,
            FuseVersion = 4,
            HardwareType = 5,
            HardwareState = 6,
            IsRecoveryBoot = 7,
            DeviceId = 8,
            BootReason = 9,
            MemoryMode = 10,
            IsDevelopmentFunctionEnabled = 11,
            KernelConfiguration = 12,
            IsChargerHiZModeEnabled = 13,
            IsQuest = 14,
            RegulatorType = 15,
            DeviceUniqueKeyGeneration = 16,
            Package2Hash = 17,
            ExosphereApiVersion = 65000,
            ExosphereNeedsReboot = 65001,
            ExosphereNeedsShutdown = 65002,
            ExosphereGitCommitHash = 65003,
            ExosphereHasRcmBugPatch = 65004,
            ExosphereBlankProdInfo = 65005,
            ExosphereAllowCalWrites = 65006,
            ExosphereEmummcType = 65007,
            ExospherePayloadAddress = 65008,
            ExosphereLogConfiguration = 65009,
            ExosphereForceEnableUsb30 = 65010,
        };

        auto item{request.Pop<ConfigItem>()};

        u64 value{};
        switch (item) {
            case ConfigItem::DisableProgramVerification:
            case ConfigItem::DramId:
            case ConfigItem::SecurityEngineInterruptNumber:
            case ConfigItem::FuseVersion:
            case ConfigItem::IsRecoveryBoot:
            case ConfigItem::DeviceId:
            case ConfigItem::BootReason:
            case ConfigItem::IsDevelopmentFunctionEnabled:
            case ConfigItem::KernelConfiguration:
            case ConfigItem::IsChargerHiZModeEnabled:
            case ConfigItem::IsQuest:
                value = 0;
                break;
            case ConfigItem::HardwareType:
                value = 0; // Icosa (the original Tegra X1 Switch)
                break;
            case ConfigItem::HardwareState:
                value = 1; // Retail
                break;
            case ConfigItem::MemoryMode:
                value = 0; // 4 GiB of RAM
                break;
            default:
                // Not available from the secure monitor of a retail console (Exosphere items and the ones that need a newer firmware)
                LOGW("Unsupported secure monitor config item: {}", static_cast<u32>(item));
                return kernel::result::NotImplemented;
        }

        response.Push<u64>(value);
        return {};
    }

    Result ISecureProcessor::GenerateRandomBytes(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto &outBuf{request.outputBuf.at(0)};

        util::FillRandomBytes(outBuf);
        return {};
    }

    Result ISecureProcessor::IsDevelopment(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push<u8>(0);
        return {};
    }
}
