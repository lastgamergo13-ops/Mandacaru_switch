// SPDX-License-Identifier: MPL-2.0

#include <sys/mman.h>
#include <vector>
#include <kernel/types/KProcess.h>
#include "gl_guest_memory.h"

namespace skyline::gpu::gl {
    void GuestMirror::Release() {
        if (aligned.valid())
            munmap(aligned.data(), aligned.size());
        aligned = span<u8>{};
        data = span<u8>{};
    }

    GuestMirror CreateGuestMirror(const DeviceState &state, const boost::container::small_vector<span<u8>, 3> &mappings) {
        GuestMirror mirror;
        if (mappings.empty())
            return mirror;

        if (mappings.size() == 1) {
            auto mapping{mappings.front()};
            u8 *alignedData{util::AlignDown(mapping.data(), constant::PageSize)};
            size_t alignedSize{static_cast<size_t>(util::AlignUp(mapping.data() + mapping.size(), constant::PageSize) - alignedData)};

            mirror.aligned = state.process->memory.CreateMirror(span<u8>{alignedData, alignedSize});
            mirror.data = mirror.aligned.subspan(static_cast<size_t>(mapping.data() - alignedData), mapping.size());
        } else {
            std::vector<span<u8>> alignedMappings;

            const auto &frontMapping{mappings.front()};
            u8 *alignedData{util::AlignDown(frontMapping.data(), constant::PageSize)};
            alignedMappings.emplace_back(alignedData, (frontMapping.data() + frontMapping.size()) - alignedData);

            size_t totalSize{frontMapping.size()};
            for (auto it{std::next(mappings.begin())}; it != std::prev(mappings.end()); ++it) {
                alignedMappings.emplace_back(it->data(), it->size());
                totalSize += it->size();
            }

            const auto &backMapping{mappings.back()};
            totalSize += backMapping.size();
            alignedMappings.emplace_back(backMapping.data(), util::AlignUp(backMapping.size(), constant::PageSize));

            mirror.aligned = state.process->memory.CreateMirrors(alignedMappings);
            mirror.data = mirror.aligned.subspan(static_cast<size_t>(frontMapping.data() - alignedData), totalSize);
        }

        return mirror;
    }
}
