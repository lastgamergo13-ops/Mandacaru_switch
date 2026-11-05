// SPDX-License-Identifier: MPL-2.0

#include <soc.h>
#include "eden/video_core/host1x/host1x.h"

namespace Tegra::Host1x {
    void GmmuManager::ReadBlock(u64 address, void *destination, size_t size) const {
        state->soc->smmu.Read(static_cast<u8 *>(destination), static_cast<u32>(address), static_cast<u32>(size));
    }

    void GmmuManager::WriteBlock(u64 address, const void *source, size_t size) const {
        state->soc->smmu.Write(static_cast<u32>(address), const_cast<u8 *>(static_cast<const u8 *>(source)), static_cast<u32>(size));
    }
}
