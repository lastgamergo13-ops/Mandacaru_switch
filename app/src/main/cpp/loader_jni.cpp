// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include "skyline/crypto/key_store.h"
#include "skyline/vfs/nca.h"
#include "skyline/vfs/os_backing.h"
#include "skyline/vfs/os_filesystem.h"
#include "skyline/vfs/partition_filesystem.h"
#include "skyline/vfs/ticket.h"
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <unistd.h>
#include "skyline/loader/nro.h"
#include "skyline/loader/nso.h"
#include "skyline/loader/nca.h"
#include "skyline/loader/xci.h"
#include "skyline/loader/nsp.h"
#include "skyline/jvm.h"

extern "C" JNIEXPORT jint JNICALL Java_org_stratoemu_strato_loader_RomFile_populate(JNIEnv *env, jobject thiz, jint jformat, jint fd, jstring appFilesPathJstring, jstring publicFilesPathJstring, jint systemLanguage) {
    skyline::signal::ScopedStackBlocker stackBlocker;

    skyline::loader::RomFormat format{static_cast<skyline::loader::RomFormat>(jformat)};

    std::shared_ptr<skyline::crypto::KeyStore> keyStore;
    std::unique_ptr<skyline::loader::Loader> loader;
    try {
        keyStore = std::make_shared<skyline::crypto::KeyStore>(skyline::JniString(env, appFilesPathJstring));
        auto backing{std::make_shared<skyline::vfs::OsBacking>(fd)};

        switch (format) {
            case skyline::loader::RomFormat::NRO:
                loader = std::make_unique<skyline::loader::NroLoader>(backing);
                break;
            case skyline::loader::RomFormat::NSO:
                loader = std::make_unique<skyline::loader::NsoLoader>(backing);
                break;
            case skyline::loader::RomFormat::NCA:
                loader = std::make_unique<skyline::loader::NcaLoader>(backing, keyStore);
                break;
            case skyline::loader::RomFormat::XCI:
                loader = std::make_unique<skyline::loader::XciLoader>(backing, keyStore);
                break;
            case skyline::loader::RomFormat::NSP:
                loader = std::make_unique<skyline::loader::NspLoader>(backing, keyStore);
                break;
            default:
                return static_cast<jint>(skyline::loader::LoaderResult::ParsingError);
        }
    } catch (const skyline::loader::loader_exception &e) {
        return static_cast<jint>(e.error);
    } catch (const std::exception &e) {
        return static_cast<jint>(skyline::loader::LoaderResult::ParsingError);
    }

    // The version and the name of the installed update are what the library shows
    try {
        loader->ApplyUpdate(skyline::JniString(env, publicFilesPathJstring), keyStore);
    } catch (const std::exception &) {
        // The game is listed without the update
    }

    try {
    jclass clazz{env->GetObjectClass(thiz)};
    jfieldID applicationNameField{env->GetFieldID(clazz, "applicationName", "Ljava/lang/String;")};
    jfieldID applicationTitleIdField{env->GetFieldID(clazz, "applicationTitleId", "Ljava/lang/String;")};
    jfieldID applicationAuthorField{env->GetFieldID(clazz, "applicationAuthor", "Ljava/lang/String;")};
    jfieldID rawIconField{env->GetFieldID(clazz, "rawIcon", "[B")};
    jfieldID applicationVersionField{env->GetFieldID(clazz, "applicationVersion", "Ljava/lang/String;")};

    if (loader->nacp) {
        auto language{skyline::language::GetApplicationLanguage(static_cast<skyline::language::SystemLanguage>(systemLanguage))};
        if (((1 << static_cast<skyline::u32>(language)) & loader->nacp->supportedTitleLanguages) == 0)
            language = loader->nacp->GetFirstSupportedTitleLanguage();

        env->SetObjectField(thiz, applicationNameField, env->NewStringUTF(loader->nacp->GetApplicationName(language).c_str()));
        env->SetObjectField(thiz, applicationVersionField, env->NewStringUTF(loader->nacp->GetApplicationVersion().c_str()));
        env->SetObjectField(thiz, applicationTitleIdField, env->NewStringUTF(loader->nacp->GetSaveDataOwnerId().c_str()));
        env->SetObjectField(thiz, applicationAuthorField, env->NewStringUTF(loader->nacp->GetApplicationPublisher(language).c_str()));

        auto icon{loader->GetIcon(language)};
        jbyteArray iconByteArray{env->NewByteArray(static_cast<jsize>(icon.size()))};
        env->SetByteArrayRegion(iconByteArray, 0, static_cast<jsize>(icon.size()), reinterpret_cast<const jbyte *>(icon.data()));
        env->SetObjectField(thiz, rawIconField, iconByteArray);
    }
    } catch (const std::exception &e) {
        // Reading the metadata (NACP strings, icon) failed, the file is listed as invalid instead of an exception escaping into the JVM and killing the app
        return static_cast<jint>(skyline::loader::LoaderResult::ParsingError);
    }

    return static_cast<jint>(skyline::loader::LoaderResult::Success);
}

extern "C" JNIEXPORT jstring Java_org_stratoemu_strato_preference_FirmwareImportPreference_fetchFirmwareVersion(JNIEnv *env, jobject thiz, jstring systemArchivesPathJstring, jstring keysPathJstring) {
    struct SystemVersion {
        skyline::u8 major;
        skyline::u8 minor;
        skyline::u8 micro;
        skyline::u8 _pad0_;
        skyline::u8 revisionMajor;
        skyline::u8 revisionMinor;
        skyline::u8 _pad1_[2];
        skyline::u8 platformString[0x20];
        skyline::u8 versionHash[0x40];
        skyline::u8 displayVersion[0x18];
        skyline::u8 displayTitle[0x80];
    };

    constexpr skyline::u64 systemVersionProgramId{0x0100000000000809};

    // C++ exceptions must never escape a JNI function (they abort the whole app), NCAs that can't be parsed (e.g. missing keys) are skipped
    try {
        auto systemArchivesFileSystem{std::make_shared<skyline::vfs::OsFileSystem>(skyline::JniString(env, systemArchivesPathJstring))};
        auto systemArchives{systemArchivesFileSystem->OpenDirectory("")};
        auto keyStore{std::make_shared<skyline::crypto::KeyStore>(skyline::JniString(env, keysPathJstring))};

        for (const auto &entry : systemArchives->Read()) {
            try {
                std::shared_ptr<skyline::vfs::Backing> backing{systemArchivesFileSystem->OpenFile(entry.name)};
                auto nca{skyline::vfs::NCA(backing, keyStore)};

                if (nca.header.programId == systemVersionProgramId && nca.romFs != nullptr) {
                    auto controlRomFs{std::make_shared<skyline::vfs::RomFileSystem>(nca.romFs)};
                    auto file{controlRomFs->OpenFile("file")};
                    SystemVersion systemVersion;
                    file->Read<SystemVersion>(systemVersion);
                    return env->NewStringUTF(reinterpret_cast<char *>(systemVersion.displayVersion));
                }
            } catch (const std::exception &) {
                continue;
            }
        }
    } catch (const std::exception &) {
    }

    return env->NewStringUTF("");
}

std::vector<skyline::u8> decodeBfttfFont(const std::shared_ptr<skyline::vfs::Backing> bfttfFile){
    constexpr skyline::u32 fontKey{0x06186249};
    constexpr skyline::u32 BFTTFMagic{0x18029a7f};

    auto firstBytes{bfttfFile->Read<skyline::u32>()};
    auto firstBytesXor{firstBytes ^ fontKey};

    if (firstBytesXor == BFTTFMagic) {
        constexpr size_t initialOffset{8};
        std::vector<skyline::u8> font(bfttfFile->size - initialOffset);

        for (size_t offset = initialOffset; offset < bfttfFile->size; offset += 4) {
            skyline::u32 decodedData{bfttfFile->Read<skyline::u32>(offset) ^ fontKey};

            font[offset - 8] = static_cast<skyline::u8>(decodedData >> 0);
            font[offset - 7] = static_cast<skyline::u8>(decodedData >> 8);
            font[offset - 6] = static_cast<skyline::u8>(decodedData >> 16);
            font[offset - 5] = static_cast<skyline::u8>(decodedData >> 24);
        }

        return font;
    }
    return {};
}

extern "C" JNIEXPORT void Java_org_stratoemu_strato_preference_FirmwareImportPreference_extractFonts(JNIEnv *env, jobject thiz, jstring systemArchivesPathJstring, jstring keysPathJstring, jstring fontsPath) {
    // Fonts are stored in the following NCAs
    // 0x0100000000000810 -> "FontNintendoExtended"
    // 0x0100000000000811 -> "FontStandard"
    // 0x0100000000000812 -> "FontKorean"
    // 0x0100000000000813 -> "FontChineseTraditional"
    // 0x0100000000000814 -> "FontChineseSimplified"

    constexpr skyline::u64 firstFontProgramId{0x0100000000000810};
    constexpr skyline::u64 lastFontProgramId{0x0100000000000814};

    const std::map<std::string, std::string> sharedFontFilenameDictionary = {
        {"nintendo_ext_003.bfttf", "FontNintendoExtended"},
        {"nintendo_ext2_003.bfttf", "FontNintendoExtended2"},
        {"nintendo_udsg-r_std_003.bfttf", "FontStandard"},
        {"nintendo_udsg-r_ko_003.bfttf", "FontKorean"},
        {"nintendo_udjxh-db_zh-tw_003.bfttf", "FontChineseTraditional"},
        {"nintendo_udsg-r_org_zh-cn_003.bfttf", "FontChineseSimplified"},
        {"nintendo_udsg-r_ext_zh-cn_003.bfttf", "FontExtendedChineseSimplified"}
    };

    // C++ exceptions must never escape a JNI function (they abort the whole app)
    try {
        auto fontsFileSystem{std::make_shared<skyline::vfs::OsFileSystem>(skyline::JniString(env, fontsPath))};
        auto systemArchivesFileSystem{std::make_shared<skyline::vfs::OsFileSystem>(skyline::JniString(env, systemArchivesPathJstring))};
        auto systemArchives{systemArchivesFileSystem->OpenDirectory("")};
        auto keyStore{std::make_shared<skyline::crypto::KeyStore>(skyline::JniString(env, keysPathJstring))};

        for (const auto &entry : systemArchives->Read()) {
            try {
                std::shared_ptr<skyline::vfs::Backing> backing{systemArchivesFileSystem->OpenFile(entry.name)};
                auto nca{skyline::vfs::NCA(backing, keyStore)};

                if (nca.header.programId >= firstFontProgramId && nca.header.programId <= lastFontProgramId && nca.romFs != nullptr) {
                    auto controlRomFs{std::make_shared<skyline::vfs::RomFileSystem>(nca.romFs)};

                    for (auto fileEntry = controlRomFs->fileMap.begin(); fileEntry != controlRomFs->fileMap.end(); fileEntry++) {
                        auto fileName{fileEntry->first};
                        auto bfttfFile{controlRomFs->OpenFile(fileName)};

                        auto decodedFont{decodeBfttfFont(bfttfFile)};
                        if (decodedFont.empty())
                            continue;

                        // Newer firmwares may ship fonts that aren't in the dictionary, skip them instead of throwing
                        auto fontName{sharedFontFilenameDictionary.find(fileName)};
                        if (fontName == sharedFontFilenameDictionary.end())
                            continue;

                        auto ttfFileName{fontName->second + ".ttf"};
                        if (fontsFileSystem->FileExists(ttfFileName))
                            fontsFileSystem->DeleteFile(ttfFileName);

                        fontsFileSystem->CreateFile(ttfFileName, decodedFont.size());
                        std::shared_ptr<skyline::vfs::Backing> ttfFile{fontsFileSystem->OpenFile(ttfFileName, {true, true, false})};

                        ttfFile->Write(decodedFont);
                    }
                }
            } catch (const std::exception &) {
                continue;
            }
        }
    } catch (const std::exception &) {
    }
}

/**
 * @brief Installs the content of an NSP (DLC, updates) so games can use it
 * @param fd A file descriptor of the NSP, this function takes ownership of it and closes it
 * @param contentsPathJstring The directory the NCAs are copied to
 * @param titleKeysPathJstring The file the title keys of the tickets in the NSP are added to ('rights id=title key' lines)
 * @return The number of NCAs that were copied, -1 if the NSP couldn't be read
 */
extern "C" JNIEXPORT jint Java_org_stratoemu_strato_utils_GameContentManager_installNsp(JNIEnv *env, jobject thiz, jint fd, jstring contentsPathJstring, jstring titleKeysPathJstring) {
    // C++ exceptions must never escape a JNI function (they abort the whole app)
    try {
        std::string contentsPath{skyline::JniString(env, contentsPathJstring)};
        std::string titleKeysPath{skyline::JniString(env, titleKeysPathJstring)};

        if (fd < 0)
            return -1;

        auto backing{std::make_shared<skyline::vfs::OsBacking>(fd, true)};
        auto nsp{std::make_shared<skyline::vfs::PartitionFileSystem>(backing)};
        std::filesystem::create_directories(contentsPath);

        auto hex{[](const auto &bytes) {
            std::string out;
            for (auto byte : bytes)
                out += fmt::format("{:02X}", byte);
            return out;
        }};

        // The keys that are already known aren't added twice
        std::set<std::string> knownKeys;
        {
            std::ifstream known{titleKeysPath};
            std::string line;
            while (std::getline(known, line))
                knownKeys.insert(line);
        }

        jint copied{};
        std::vector<skyline::u8> buffer(4 * 1024 * 1024);
        for (const auto &entry : nsp->OpenDirectory("", {false, true})->Read()) {
            std::string extension{entry.name.substr(entry.name.find_last_of('.') + 1)};
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return std::tolower(c); });

            if (extension == "nca") {
                auto nca{nsp->OpenFile(entry.name)};
                std::string target{contentsPath + "/" + std::filesystem::path(entry.name).filename().string()};
                std::ofstream output{target, std::ios::binary | std::ios::trunc};
                if (!output)
                    return -1;

                for (size_t offset{}; offset < nca->size; offset += buffer.size()) {
                    size_t length{std::min(buffer.size(), nca->size - offset)};
                    nca->ReadUnchecked(skyline::span<skyline::u8>(buffer.data(), length), offset);
                    output.write(reinterpret_cast<const char *>(buffer.data()), static_cast<std::streamsize>(length));
                }
                copied++;
            } else if (extension == "tik") {
                try {
                    skyline::vfs::Ticket ticket{nsp->OpenFile(entry.name)};
                    std::string line{hex(ticket.rightsId) + "=" + hex(std::span<const skyline::u8>(ticket.titleKeyBlock.data(), 16))};
                    if (knownKeys.insert(line).second) {
                        std::filesystem::create_directories(std::filesystem::path(titleKeysPath).parent_path());
                        std::ofstream keys{titleKeysPath, std::ios::app};
                        keys << line << "\n";
                    }
                } catch (const std::exception &) {
                    // A ticket that can't be read only affects the content that needs it
                }
            }
        }

        return copied;
    } catch (const std::exception &) {
        return -1;
    }
}
