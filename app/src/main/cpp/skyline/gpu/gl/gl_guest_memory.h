// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <utility>
#include <boost/container/small_vector.hpp>
#include <common.h>

namespace skyline::gpu::gl {
    /**
     * @brief A mapping of the guest memory of a texture or a buffer that can be read without triggering its traps (the traps protect the guest's mapping, not this alias of the same pages)
     */
    struct GuestMirror {
        span<u8> aligned; //!< The pages of the mirror, the ones that have to be unmapped
        span<u8> data; //!< The bytes of the guest memory in order, even when the guest memory is in several mappings

        GuestMirror() = default;

        GuestMirror(const GuestMirror &) = delete;

        GuestMirror &operator=(const GuestMirror &) = delete;

        GuestMirror(GuestMirror &&other) noexcept : aligned{std::exchange(other.aligned, span<u8>{})}, data{std::exchange(other.data, span<u8>{})} {}

        GuestMirror &operator=(GuestMirror &&other) noexcept {
            Release();
            aligned = std::exchange(other.aligned, span<u8>{});
            data = std::exchange(other.data, span<u8>{});
            return *this;
        }

        ~GuestMirror() {
            Release();
        }

        /**
         * @brief Unmaps the mirror
         */
        void Release();
    };

    /**
     * @brief Creates a mirror of the guest memory mappings, the same way a Vulkan Texture does
     * @param mappings The spans of guest memory in order, the first and the last can start and end in the middle of a page
     */
    GuestMirror CreateGuestMirror(const DeviceState &state, const boost::container::small_vector<span<u8>, 3> &mappings);
}
