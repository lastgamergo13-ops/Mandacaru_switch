// SPDX-License-Identifier: MPL-2.0

#include <loader/loader.h>
#include "ncz_backing.h"

namespace skyline::vfs {
    constexpr u64 NcaHeaderSize{0x4000}; //!< The size of the NCA header region that is stored as-is at the start of an NCZ
    constexpr u64 SectionEntrySize{0x40}; //!< offset, size, crypto type and padding (u64 each), then the key and the counter (16 bytes each)
    constexpr u64 MaxSections{64};
    constexpr u64 MinBlockSizeExponent{14};
    constexpr u64 MaxBlockSizeExponent{26}; //!< A block is kept in memory after being decompressed, 64 MiB is already more than any sane NCZ uses
    constexpr u64 ZstdCompressionType{1}; //!< The only compression type NCZBLOCK defines
    constexpr u64 CtrCryptoType{3};
    constexpr u64 BktrCryptoType{4};

    using loader::LoaderResult;
    using loader::loader_exception;

    NczBacking::NczBacking(std::shared_ptr<Backing> pBacking) : Backing({true, false, false}, 0), backing(std::move(pBacking)), decompressionContext(ZSTD_createDCtx()) {
        if (decompressionContext == nullptr)
            throw exception("Failed to create the zstd decompression context");

        auto fail{[](const char *message) { throw loader_exception(LoaderResult::ParsingError, message); }};

        u64 cursor{NcaHeaderSize};
        if (backing->size < cursor + 0x10)
            fail("NCZ file is too small");

        std::array<char, 8> magic{backing->Read<std::array<char, 8>>(cursor)};
        if (std::memcmp(magic.data(), "NCZSECTN", 8) != 0)
            fail("NCZ section table is missing");
        u64 sectionCount{backing->Read<u64>(cursor + 8)};
        cursor += 0x10;
        if (sectionCount == 0 || sectionCount > MaxSections || cursor + sectionCount * SectionEntrySize > backing->size)
            fail("NCZ section table is invalid");

        u64 sectionsEnd{};
        for (u64 i{}; i < sectionCount; i++) {
            struct Entry {
                u64 offset;
                u64 size;
                u64 cryptoType;
                u64 _pad_;
                crypto::KeyStore::Key128 key;
                crypto::KeyStore::Key128 counter;
            };
            static_assert(sizeof(Entry) == SectionEntrySize);

            auto entry{backing->Read<Entry>(cursor + i * SectionEntrySize)};
            if (entry.offset + entry.size < entry.offset)
                fail("NCZ section is invalid");
            sectionsEnd = std::max(sectionsEnd, entry.offset + entry.size);

            bool encrypted{entry.cryptoType == CtrCryptoType || entry.cryptoType == BktrCryptoType};
            sections.push_back(Section{entry.offset, entry.size, entry.counter, encrypted ? std::make_unique<crypto::AesCipher>(entry.key, MBEDTLS_CIPHER_AES_128_CTR) : nullptr});
        }
        cursor += sectionCount * SectionEntrySize;

        if (sectionsEnd <= NcaHeaderSize)
            fail("NCZ sections are empty");

        // The block table directly follows, if it is not there then the rest of the file is a single zstd stream (a "solid" NCZ) that can't be read from the middle
        if (backing->size < cursor + 0x18)
            fail("NCZ file is truncated");
        magic = backing->Read<std::array<char, 8>>(cursor);
        if (std::memcmp(magic.data(), "NCZBLOCK", 8) != 0)
            throw loader_exception(LoaderResult::UnsupportedCompression, "Solid NCZ files are not supported, only block compressed ones");

        struct BlockHeader {
            std::array<char, 8> magic;
            u8 version;
            u8 type;
            u8 _pad_;
            u8 blockSizeExponent;
            u32 blockCount;
            u64 decompressedSize;
        };
        static_assert(sizeof(BlockHeader) == 0x18);

        auto blockHeader{backing->Read<BlockHeader>(cursor)};
        cursor += sizeof(BlockHeader);
        if (blockHeader.type != ZstdCompressionType)
            fail("NCZ block compression type is unsupported");
        if (blockHeader.blockSizeExponent < MinBlockSizeExponent || blockHeader.blockSizeExponent > MaxBlockSizeExponent)
            fail("NCZ block size is unsupported");
        if (blockHeader.blockCount == 0 || cursor + static_cast<u64>(blockHeader.blockCount) * sizeof(u32) > backing->size)
            fail("NCZ block table is invalid");

        blockSize = u64{1} << blockHeader.blockSizeExponent;

        std::vector<u32> compressedSizes(blockHeader.blockCount);
        backing->Read(span(compressedSizes), cursor);
        cursor += static_cast<u64>(blockHeader.blockCount) * sizeof(u32);

        blockOffsets.reserve(blockHeader.blockCount + 1);
        blockOffsets.push_back(cursor);
        for (u32 compressedSize : compressedSizes) {
            if (compressedSize == 0 || compressedSize > blockSize)
                fail("NCZ block size is invalid");
            blockOffsets.push_back(blockOffsets.back() + compressedSize);
        }
        if (blockOffsets.back() > backing->size)
            fail("NCZ file is truncated");

        // The decompressed length is taken from the section table, with the header field as a fallback, whichever agrees with the block count
        u64 blockCount{blockHeader.blockCount};
        auto fitsBlocks{[&](u64 length) { return length > (blockCount - 1) * blockSize && length <= blockCount * blockSize; }};
        if (fitsBlocks(sectionsEnd - NcaHeaderSize))
            streamLength = sectionsEnd - NcaHeaderSize;
        else if (fitsBlocks(blockHeader.decompressedSize))
            streamLength = blockHeader.decompressedSize;
        else if (blockHeader.decompressedSize > NcaHeaderSize && fitsBlocks(blockHeader.decompressedSize - NcaHeaderSize))
            streamLength = blockHeader.decompressedSize - NcaHeaderSize;
        else
            fail("NCZ block table doesn't match the decompressed size");

        size = NcaHeaderSize + streamLength;
    }

    NczBacking::~NczBacking() {
        ZSTD_freeDCtx(decompressionContext);
    }

    const std::vector<u8> &NczBacking::GetBlock(u64 index) {
        for (auto &entry : cache)
            if (entry.index == index)
                return entry.data;

        auto &slot{cache[nextCacheSlot]};
        nextCacheSlot = (nextCacheSlot + 1) % cache.size();
        slot.index = std::numeric_limits<u64>::max(); // Don't leave a half-filled block marked as valid if something throws

        u64 plainSize{std::min(blockSize, streamLength - index * blockSize)};
        u64 compressedSize{blockOffsets[index + 1] - blockOffsets[index]};
        slot.data.resize(plainSize);

        if (compressedSize == plainSize) {
            // Blocks that didn't shrink are stored as they are
            backing->Read(slot.data, blockOffsets[index]);
        } else {
            compressedBuffer.resize(compressedSize);
            backing->Read(compressedBuffer, blockOffsets[index]);

            size_t result{ZSTD_decompressDCtx(decompressionContext, slot.data.data(), plainSize, compressedBuffer.data(), compressedSize)};
            if (ZSTD_isError(result))
                throw exception("Failed to decompress NCZ block {}: {}", index, ZSTD_getErrorName(result));
            if (result != plainSize)
                throw exception("NCZ block {} decompressed to 0x{:X} bytes instead of 0x{:X}", index, result, plainSize);
        }

        slot.index = index;
        return slot.data;
    }

    void NczBacking::Encrypt(span<u8> data, u64 ncaOffset) {
        u64 end{ncaOffset + data.size()};
        for (auto &section : sections) {
            if (!section.cipher)
                continue;

            u64 start{std::max(ncaOffset, section.offset)};
            u64 stop{std::min(end, section.offset + section.size)};
            if (start >= stop)
                continue;

            // AES-CTR works on 16 byte blocks with the counter set from the absolute offset of the block, so an unaligned start is padded to the block start
            u64 alignedStart{start & ~u64{0xF}};
            size_t padding{static_cast<size_t>(start - alignedStart)};
            scratch.assign(padding + (stop - start), 0);
            std::memcpy(scratch.data() + padding, data.data() + (start - ncaOffset), stop - start);

            auto counter{section.counter};
            u64 counterLE{util::SwapEndianness(alignedStart >> 4)};
            std::memcpy(counter.data() + 8, &counterLE, 8);

            section.cipher->SetIV(counter);
            section.cipher->Decrypt(scratch); // CTR mode is symmetric, "decrypting" is what encrypts it again

            std::memcpy(data.data() + (start - ncaOffset), scratch.data() + padding, stop - start);
        }
    }

    size_t NczBacking::ReadImpl(span<u8> output, size_t offset) {
        if (offset >= size)
            return 0;

        size_t total{std::min<size_t>(output.size(), size - offset)};
        size_t done{};

        if (offset < NcaHeaderSize) {
            size_t headerRead{std::min<size_t>(total, NcaHeaderSize - offset)};
            if (backing->ReadUnchecked(output.subspan(0, headerRead), offset) != headerRead)
                return 0;
            done += headerRead;
            offset += headerRead;
        }

        std::scoped_lock guard{mutex};
        while (done < total) {
            u64 streamOffset{offset - NcaHeaderSize};
            u64 blockIndex{streamOffset / blockSize};
            u64 blockOffset{streamOffset % blockSize};

            const auto &block{GetBlock(blockIndex)};
            size_t chunk{std::min<size_t>(total - done, block.size() - blockOffset)};

            auto destination{output.subspan(done, chunk)};
            std::memcpy(destination.data(), block.data() + blockOffset, chunk);
            Encrypt(destination, offset);

            done += chunk;
            offset += chunk;
        }

        return done;
    }
}
