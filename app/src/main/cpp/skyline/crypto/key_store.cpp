// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <vfs/os_filesystem.h>
#include <cstdlib>
#include "key_store.h"

namespace skyline::crypto {
    KeyStore::KeyStore(const std::string &rootPath) {
        vfs::OsFileSystem root(rootPath);
        if (root.FileExists("title.keys"))
            ReadPairs(root.OpenFile("title.keys"), &KeyStore::PopulateTitleKeys);
        if (root.FileExists("prod.keys"))
            ReadPairs(root.OpenFile("prod.keys"), &KeyStore::PopulateKeys);
    }

    void KeyStore::ReadPairs(const std::shared_ptr<vfs::Backing> &backing, ReadPairsCallback callback) {
        std::vector<char> fileContent(backing->size);
        backing->Read(span(fileContent));

        // A key file from outside the app can have Windows line endings, blank lines, comments, no newline after the last line or a malformed line: none of these should stop every other key from loading (an exception here used to escape into the game scan and take the whole app down)
        auto lineStart{fileContent.begin()};
        while (lineStart != fileContent.end()) {
            auto lineEnd{std::find(lineStart, fileContent.end(), '\n')};
            auto next{lineEnd == fileContent.end() ? lineEnd : lineEnd + 1};

            while (lineEnd != lineStart && (*(lineEnd - 1) == '\r' || *(lineEnd - 1) == ' ' || *(lineEnd - 1) == '\t'))
                lineEnd--;

            auto keyEnd{std::find(lineStart, lineEnd, '=')};
            if (keyEnd != lineEnd && *lineStart != ';') {
                std::string_view key(&*lineStart, static_cast<size_t>(keyEnd - lineStart));
                std::string_view value(&*(keyEnd + 1), static_cast<size_t>(lineEnd - keyEnd - 1));
                while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
                    key.remove_suffix(1);
                while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
                    value.remove_prefix(1);

                try {
                    (this->*callback)(key, value);
                } catch (const std::exception &e) {
                    // Skip just this line
                }
            }

            lineStart = next;
        }
    }

    void KeyStore::PopulateTitleKeys(std::string_view keyName, std::string_view value) {
        Key128 key{util::HexStringToArray<16>(keyName)};
        Key128 valueArray{util::HexStringToArray<16>(value)};
        titleKeys.emplace(key, valueArray);
    }

    void KeyStore::PopulateTitleKey(Key128 keyName, Key128 value) {
        if (!titleKeys.contains(keyName))
            titleKeys.emplace(keyName, value);
    }

    void KeyStore::PopulateKeys(std::string_view keyName, std::string_view value) {
        {
            auto it{key256Names.find(keyName)};
            if (it != key256Names.end()) {
                it->second = headerKey = util::HexStringToArray<32>(value);
                return;
            }
        }

        if (keyName.size() > 2) {
            auto it{indexedKey128Names.find(keyName.substr(0, keyName.size() - 2))};
            if (it != indexedKey128Names.end()) {
                std::string indexString{keyName.substr(it->first.size())};
                char *indexEnd{};
                size_t index{std::strtoul(indexString.c_str(), &indexEnd, 16)};
                // Ignore malformed indices and key generations we can't store, writing out of bounds here used to corrupt neighbouring keys and the title key map
                if (indexString.empty() || *indexEnd != '\0' || index >= it->second.size())
                    return;
                it->second[index] = util::HexStringToArray<16>(value);
            }
        }
    }
}
