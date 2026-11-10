// SPDX-License-Identifier: MPL-2.0
// Copyright © 2021 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <bit>
#include <map>
#include <mutex>
#include <common/address_space.h>

namespace skyline::soc::gm20b {
    static constexpr u8 GmmuAddressSpaceBits{40}; //!< The size of the GMMU AS in bits
    static constexpr size_t GmmuSmallPageSize{0x1000}; // 4KiB
    static constexpr size_t GmmuSmallPageSizeBits{std::countr_zero(GmmuSmallPageSize)};
    static constexpr size_t GmmuMinBigPageSize{0x20000}; // 128KiB
    static constexpr size_t GmmuMinBigPageSizeBits{std::countr_zero(GmmuMinBigPageSize)};


    /**
     * @brief The GMMU (Graphics Memory Management Unit) class handles mapping between a Maxwell GPU virtual address space and an application's address space and is meant to roughly emulate the GMMU on the X1
     * @note This is not accurate to the X1 as it would have an SMMU between the GMMU and physical memory but we don't need to emulate this abstraction
     * @note The GMMU is implemented entirely as a template specialization over FlatMemoryManager
     */
    using GMMU = FlatMemoryManager<u64, 0, GmmuAddressSpaceBits, GmmuSmallPageSizeBits, GmmuMinBigPageSizeBits>;

    /**
     * @brief The values of the 'kind' that is given to the GPU when mapping memory, which tells how the contents of the memory are laid out
     * @note Only the values that tell whether memory is linear are needed, all others are block linear in some way
     * @url https://github.com/envytools/envytools/blob/master/docs/hw/memory/g80-vm.rst
     */
    namespace pte_kind {
        constexpr u8 Pitch{0x0};
        constexpr u8 PitchNoSwizzle{0xFD};
        constexpr u8 Invalid{0xFF}; //!< The kind of memory that wasn't mapped with one

        constexpr bool IsPitch(u8 kind) {
            return kind == Pitch || kind == PitchNoSwizzle;
        }
    }

    /**
     * @brief Keeps the 'kind' that the GPU address space was mapped with, the GMMU itself doesn't store it
     */
    class KindMap {
      private:
        struct Entry {
            u64 size;
            u8 kind;
        };

        std::mutex mutex;
        std::map<u64, Entry> entries; //!< Ranges that don't overlap, by their start address

        void EraseLocked(u64 start, u64 size) {
            u64 end{start + size};

            auto it{entries.upper_bound(start)};
            if (it != entries.begin())
                --it;

            while (it != entries.end() && it->first < end) {
                u64 entryStart{it->first}, entryEnd{it->first + it->second.size};
                Entry entry{it->second};

                if (entryEnd <= start) {
                    ++it;
                    continue;
                }

                it = entries.erase(it);

                // Keep the parts of the range that are outside of the erased one
                if (entryStart < start)
                    entries[entryStart] = {start - entryStart, entry.kind};
                if (entryEnd > end)
                    it = entries.emplace(end, Entry{entryEnd - end, entry.kind}).first;
            }
        }

      public:
        void Set(u64 start, u64 size, u8 kind) {
            if (!size)
                return;

            std::scoped_lock lock{mutex};
            EraseLocked(start, size);
            entries[start] = {size, kind};
        }

        void Clear(u64 start, u64 size) {
            if (!size)
                return;

            std::scoped_lock lock{mutex};
            EraseLocked(start, size);
        }

        /**
         * @return The kind that the address was mapped with or pte_kind::Invalid if there isn't one
         */
        u8 Get(u64 address) {
            std::scoped_lock lock{mutex};

            auto it{entries.upper_bound(address)};
            if (it == entries.begin())
                return pte_kind::Invalid;

            --it;
            if (address < it->first + it->second.size)
                return it->second.kind;
            return pte_kind::Invalid;
        }
    };

    struct AddressSpaceContext {
        GMMU gmmu;
        KindMap kinds;
    };

    /**
     * @brief A host IOVA address composed of 32-bit low/high register values
     * @note This differs from engine::Address in that it is little-endian rather than big-endian ordered for the register values
     */
    union IOVA {
        u64 iova;
        struct {
            u32 low;
            u32 high;
        };

        IOVA(u64 iova) : iova{iova} {}

        operator u64 &() {
            return iova;
        }
    };
    static_assert(sizeof(IOVA) == sizeof(u64));
}
