// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <mutex>
#include <crypto/aes_cipher.h>
#include <crypto/key_store.h>
#include "backing.h"

namespace skyline::vfs {
    /**
     * @brief A read-only "bucket tree" (BKTR), the lookup table the patch sections (updates) of NCAs use: it maps an offset to the entry that covers it
     * @note Only trees with a single level of nodes are supported, which is every tree with less than ~1.6 million entries
     * @url https://switchbrew.org/wiki/NCA#Patch_Section_Information
     */
    class BucketTree {
      public:
        static constexpr size_t NodeSize{0x4000};

        struct Found {
            const u8 *entry; //!< The entry that covers the offset, its first 8 bytes are the offset it starts at
            i64 entryOffset; //!< The offset the entry starts at
            i64 nextOffset; //!< The offset the next entry starts at (the end of the tree for the last one)
        };

      private:
        std::vector<u8> storage; //!< The node storage (one node with the offsets of the entry sets) followed by the entry sets
        size_t entrySize;
        i32 entrySetCount;

      public:
        i64 endOffset; //!< The offset the tree ends at (the size of whatever it describes)

        /**
         * @param storage The node and the entry storage of the tree, in that order
         */
        BucketTree(std::vector<u8> storage, size_t entrySize, i32 entryCount);

        /**
         * @return The entry that covers the offset, throws if the offset is outside of the tree
         */
        Found Find(i64 offset) const;
    };

    /**
     * @brief The layout of the patch information of an NCA section that was read from its header
     */
    struct BktrParameters {
        std::shared_ptr<Backing> section; //!< The section as it is stored in the NCA (encrypted unless the NCA is)
        bool encrypted;
        crypto::KeyStore::Key128 key;
        u32 secureValue;
        u32 generation; //!< The generation from the section header, used for the table of the encryption subsections
        u64 sectionOffset; //!< The offset of the section in the NCA, it is part of the AES-CTR counter
        u64 indirectOffset; //!< The offset of the relocation table in the section, the patch data is in front of it
        u64 indirectSize;
        i32 indirectEntryCount;
        u64 aesCtrExOffset; //!< The offset of the table with the encryption subsections, after the relocation table
        u64 aesCtrExSize;
        i32 aesCtrExEntryCount;
    };

    /**
     * @brief The section of an update NCA with the RomFS of the updated game: it is made from the base game's section and the new data of the update
     * @details The relocation table says which ranges come from the base section and which from the data stored in the update. The update's data is encrypted with AES-CTR where every subsection has a counter of its own
     */
    class BktrBacking : public Backing {
      private:
        BktrParameters parameters;
        std::shared_ptr<Backing> baseSection;
        std::unique_ptr<BucketTree> relocationTree;
        std::unique_ptr<BucketTree> subsectionTree;
        std::unique_ptr<crypto::AesCipher> cipher;
        std::vector<u8> scratch;
        std::mutex mutex;

        /**
         * @brief Decrypts data that was read from the section at the given offset with AES-CTR
         * @param generation The generation field of the counter
         */
        void Decrypt(span<u8> data, u64 sectionPosition, u32 generation);

        /**
         * @brief Reads the data stored in the update itself (the part of the section in front of the tables) decrypted
         */
        void ReadPatchData(span<u8> output, u64 offset);

        std::vector<u8> ReadTable(u64 offset, u64 size, bool decryptWithSubsections);

      protected:
        size_t ReadImpl(span<u8> output, size_t offset) override;

      public:
        /**
         * @param baseSection The same section of the base game, decrypted (null if the base game isn't known, the ranges that come from it are then zeroes)
         */
        BktrBacking(BktrParameters parameters, std::shared_ptr<Backing> baseSection);
    };
}
