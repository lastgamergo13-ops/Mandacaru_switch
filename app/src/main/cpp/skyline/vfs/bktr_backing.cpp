// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <loader/loader.h>
#include "bktr_backing.h"

namespace skyline::vfs {
    using loader::LoaderResult;
    using loader::loader_exception;

    constexpr size_t NodeHeaderSize{0x10}; //!< Index, count and end offset
    constexpr size_t RelocationEntrySize{0x14}; //!< Virtual offset, physical offset and the index of the storage the physical offset is in
    constexpr size_t SubsectionEntrySize{0x10}; //!< Offset, whether it's encrypted and the generation of its counter

    template<typename T>
    static T ReadAt(const u8 *pointer) {
        T value;
        std::memcpy(&value, pointer, sizeof(T));
        return value;
    }

    BucketTree::BucketTree(std::vector<u8> pStorage, size_t pEntrySize, i32 entryCount) : storage(std::move(pStorage)), entrySize(pEntrySize) {
        if (entryCount <= 0)
            throw loader_exception(LoaderResult::ParsingError, "Empty bucket tree");

        size_t entriesPerSet{(NodeSize - NodeHeaderSize) / entrySize};
        size_t offsetsPerNode{(NodeSize - NodeHeaderSize) / sizeof(i64)};
        entrySetCount = static_cast<i32>((static_cast<size_t>(entryCount) + entriesPerSet - 1) / entriesPerSet);

        if (static_cast<size_t>(entrySetCount) > offsetsPerNode)
            throw loader_exception(LoaderResult::ParsingError, "Bucket trees with several levels of nodes are not supported");
        if (storage.size() < NodeSize * (1 + static_cast<size_t>(entrySetCount)))
            throw loader_exception(LoaderResult::ParsingError, "Bucket tree storage is too small");

        i32 count{ReadAt<i32>(storage.data() + 4)};
        endOffset = ReadAt<i64>(storage.data() + 8);
        if (count < 1 || static_cast<size_t>(count) > offsetsPerNode || endOffset <= 0)
            throw loader_exception(LoaderResult::ParsingError, "Bucket tree node is invalid");
        entrySetCount = std::min(entrySetCount, count);
    }

    BucketTree::Found BucketTree::Find(i64 offset) const {
        if (offset < 0 || offset >= endOffset)
            throw exception("Offset 0x{:X} is outside of the bucket tree (0x{:X})", offset, endOffset);

        // The first node holds the offset every entry set starts at
        const u8 *offsets{storage.data() + NodeHeaderSize};
        i32 low{}, high{entrySetCount};
        while (low < high) {
            i32 middle{(low + high) / 2};
            if (ReadAt<i64>(offsets + middle * sizeof(i64)) <= offset)
                low = middle + 1;
            else
                high = middle;
        }
        i32 setIndex{low - 1};
        if (setIndex < 0)
            throw exception("Offset 0x{:X} is before the first entry of the bucket tree", offset);

        const u8 *set{storage.data() + NodeSize * (1 + static_cast<size_t>(setIndex))};
        i32 count{ReadAt<i32>(set + 4)};
        size_t maxCount{(NodeSize - NodeHeaderSize) / entrySize};
        if (count < 1 || static_cast<size_t>(count) > maxCount)
            throw exception("Entry set {} of the bucket tree is invalid", setIndex);

        const u8 *entries{set + NodeHeaderSize};
        i32 entryLow{}, entryHigh{count};
        while (entryLow < entryHigh) {
            i32 middle{(entryLow + entryHigh) / 2};
            if (ReadAt<i64>(entries + static_cast<size_t>(middle) * entrySize) <= offset)
                entryLow = middle + 1;
            else
                entryHigh = middle;
        }
        i32 entryIndex{entryLow - 1};
        if (entryIndex < 0)
            throw exception("Offset 0x{:X} is before the first entry of the entry set {}", offset, setIndex);

        const u8 *entry{entries + static_cast<size_t>(entryIndex) * entrySize};
        i64 next{endOffset};
        if (entryIndex + 1 < count)
            next = ReadAt<i64>(entry + entrySize);
        else if (setIndex + 1 < entrySetCount)
            next = ReadAt<i64>(offsets + static_cast<size_t>(setIndex + 1) * sizeof(i64));

        i64 entryOffset{ReadAt<i64>(entry)};
        if (next <= entryOffset || next > endOffset)
            next = endOffset;
        return {entry, entryOffset, next};
    }

    BktrBacking::BktrBacking(BktrParameters pParameters, std::shared_ptr<Backing> pBaseSection) : Backing({true, false, false}, 0), parameters(std::move(pParameters)), baseSection(std::move(pBaseSection)) {
        if (parameters.encrypted)
            cipher = std::make_unique<crypto::AesCipher>(parameters.key, MBEDTLS_CIPHER_AES_128_CTR);

        if (parameters.indirectSize == 0 || parameters.aesCtrExSize == 0 || parameters.indirectOffset + parameters.indirectSize > parameters.aesCtrExOffset)
            throw loader_exception(LoaderResult::ParsingError, "Invalid patch information");
        if (parameters.aesCtrExOffset + parameters.aesCtrExSize > parameters.section->size)
            throw loader_exception(LoaderResult::ParsingError, "Patch tables are outside of the section");

        // The table of the encryption subsections is stored after everything else, encrypted with the counter of the section header
        subsectionTree = std::make_unique<BucketTree>(ReadTable(parameters.aesCtrExOffset, parameters.aesCtrExSize, false), SubsectionEntrySize, parameters.aesCtrExEntryCount);
        relocationTree = std::make_unique<BucketTree>(ReadTable(parameters.indirectOffset, parameters.indirectSize, true), RelocationEntrySize, parameters.indirectEntryCount);

        size = static_cast<size_t>(relocationTree->endOffset);
    }

    void BktrBacking::Decrypt(span<u8> data, u64 sectionPosition, u32 generation) {
        if (!cipher || data.empty())
            return;

        // The counter has the secure value and the generation in its upper half and the position of the block in the NCA in the lower half, an unaligned start is padded to the start of its block
        u64 position{parameters.sectionOffset + sectionPosition};
        u64 alignedPosition{position & ~u64{0xF}};
        size_t padding{static_cast<size_t>(position - alignedPosition)};

        scratch.assign(padding + data.size(), 0);
        std::memcpy(scratch.data() + padding, data.data(), data.size());

        std::array<u8, 0x10> counter{};
        u32 secureBE{util::SwapEndianness(parameters.secureValue)};
        u32 generationBE{util::SwapEndianness(generation)};
        u64 blockBE{util::SwapEndianness(alignedPosition >> 4)};
        std::memcpy(counter.data(), &secureBE, 4);
        std::memcpy(counter.data() + 4, &generationBE, 4);
        std::memcpy(counter.data() + 8, &blockBE, 8);

        cipher->SetIV(counter);
        cipher->Decrypt(scratch);
        std::memcpy(data.data(), scratch.data() + padding, data.size());
    }

    std::vector<u8> BktrBacking::ReadTable(u64 offset, u64 size, bool decryptWithSubsections) {
        std::vector<u8> table(size);
        if (decryptWithSubsections) {
            // The relocation table is part of the data that the encryption subsections cover
            ReadPatchData(table, offset);
        } else {
            parameters.section->Read(table, offset);
            Decrypt(table, offset, parameters.generation);
        }
        return table;
    }

    void BktrBacking::ReadPatchData(span<u8> output, u64 offset) {
        if (offset + output.size() > parameters.aesCtrExOffset)
            throw exception("Patch data read is outside of the data region: 0x{:X} (0x{:X} bytes)", offset, output.size());

        size_t done{};
        while (done < output.size()) {
            u64 position{offset + done};
            auto found{subsectionTree->Find(static_cast<i64>(position))};

            size_t chunk{static_cast<size_t>(std::min<i64>(static_cast<i64>(output.size() - done), found.nextOffset - static_cast<i64>(position)))};
            auto part{output.subspan(done, chunk)};
            parameters.section->Read(part, position);

            // Entry: offset (8 bytes), encryption (1 byte, 0 means encrypted), 3 reserved bytes and the generation (4 bytes)
            u8 encryption{found.entry[8]};
            u32 generation{ReadAt<u32>(found.entry + 12)};
            if (encryption == 0)
                Decrypt(part, position, generation);

            done += chunk;
        }
    }

    size_t BktrBacking::ReadImpl(span<u8> output, size_t offset) {
        if (offset >= size)
            return 0;

        size_t total{std::min(output.size(), size - offset)};
        size_t done{};

        std::scoped_lock lock{mutex};
        while (done < total) {
            i64 position{static_cast<i64>(offset + done)};
            auto found{relocationTree->Find(position)};

            // Entry: virtual offset (8 bytes), physical offset (8 bytes) and the storage the physical offset is in (4 bytes, 0 is the base game and 1 is the update)
            i64 physicalOffset{ReadAt<i64>(found.entry + 8)};
            i32 storageIndex{ReadAt<i32>(found.entry + 16)};

            size_t chunk{static_cast<size_t>(std::min<i64>(static_cast<i64>(total - done), found.nextOffset - position))};
            auto part{output.subspan(done, chunk)};
            u64 source{static_cast<u64>(physicalOffset + (position - found.entryOffset))};

            if (storageIndex == 1) {
                ReadPatchData(part, source);
            } else if (storageIndex == 0) {
                if (baseSection && source + chunk <= baseSection->size)
                    baseSection->Read(part, source);
                else
                    std::memset(part.data(), 0, part.size());
            } else {
                throw exception("Relocation entry with an invalid storage index: {}", storageIndex);
            }

            done += chunk;
        }

        return done;
    }
}
