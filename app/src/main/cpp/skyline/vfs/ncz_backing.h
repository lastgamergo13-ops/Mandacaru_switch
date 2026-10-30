// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <array>
#include <limits>
#include <memory>
#include <vector>
#include <mutex>
#include <crypto/aes_cipher.h>
#include <crypto/key_store.h>
#include <zstd.h>
#include "backing.h"

namespace skyline::vfs {
    /**
     * @brief A backing that presents a block-compressed NCZ file (the compressed NCA format used by NSZ/XCZ) as the original NCA
     * @details The first 0x4000 bytes of an NCZ are the untouched NCA header. What follows is the section table (NCZSECTN), the block table (NCZBLOCK) and then the rest of the NCA, decrypted and compressed in independent zstd blocks. Blocks are decompressed on demand and the sections are encrypted again with AES-CTR, so the result is byte for byte the original NCA and can be read randomly without extracting anything.
     * @note NCZ files compressed as a single "solid" zstd stream (the default of the nsz tool) can't be read randomly and are rejected with LoaderResult::UnsupportedCompression
     * @url https://github.com/nicoboss/nsz
     */
    class NczBacking : public Backing {
      private:
        struct Section {
            u64 offset; //!< The offset of the section in the original NCA
            u64 size;
            crypto::KeyStore::Key128 counter; //!< The AES-CTR counter, its lower 8 bytes are replaced with the offset when used
            std::unique_ptr<crypto::AesCipher> cipher; //!< Only set for sections that were encrypted with AES-CTR in the original NCA
        };

        struct CachedBlock {
            u64 index{std::numeric_limits<u64>::max()};
            std::vector<u8> data;
        };

        std::shared_ptr<Backing> backing;
        std::vector<Section> sections;
        std::vector<u64> blockOffsets; //!< The offset of every compressed block in the NCZ file, with one extra entry for the end of the last block
        u64 blockSize{}; //!< The size of an uncompressed block (the last one can be smaller)
        u64 streamLength{}; //!< The amount of bytes after the NCA header once decompressed
        std::array<CachedBlock, 4> cache; //!< The most recently decompressed blocks
        size_t nextCacheSlot{};
        std::vector<u8> compressedBuffer;
        std::vector<u8> scratch;
        ZSTD_DCtx *decompressionContext;
        std::mutex mutex; //!< Synchronizes the cache, the buffers and the cipher state

        /**
         * @return The decompressed contents of the block, valid until the next call
         */
        const std::vector<u8> &GetBlock(u64 index);

        /**
         * @brief Encrypts the part of the data that belongs to AES-CTR sections again, in-place
         * @param ncaOffset The offset of the data in the original NCA
         */
        void Encrypt(span<u8> data, u64 ncaOffset);

      protected:
        size_t ReadImpl(span<u8> output, size_t offset) override;

      public:
        NczBacking(std::shared_ptr<Backing> backing);

        ~NczBacking();
    };
}
