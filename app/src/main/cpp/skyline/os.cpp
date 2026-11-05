// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <atomic>
#include <thread>
#include "gpu.h"
#include "nce.h"
#include "nce/guest.h"
#include "kernel/svc.h"
#include "soc/host1x/video/ffmpeg_check.h"
#include "kernel/types/KProcess.h"
#include "vfs/os_backing.h"
#include "loader/nro.h"
#include "loader/nso.h"
#include "loader/nca.h"
#include "loader/nsp.h"
#include "loader/xci.h"
#include "os.h"

namespace skyline::kernel {
    OS::OS(
        std::shared_ptr<JvmManager> &jvmManager,
        std::shared_ptr<Settings> &settings,
        std::string publicAppFilesPath,
        std::string privateAppFilesPath,
        std::string nativeLibraryPath,
        std::string deviceTimeZone,
        std::shared_ptr<vfs::FileSystem> assetFileSystem)
        : nativeLibraryPath(std::move(nativeLibraryPath)),
          publicAppFilesPath(std::move(publicAppFilesPath)),
          privateAppFilesPath(std::move(privateAppFilesPath)),
          deviceTimeZone(std::move(deviceTimeZone)),
          assetFileSystem(std::move(assetFileSystem)),
          state(this, jvmManager, settings),
          serviceManager(state) {}

    /**
     * @brief Watches for the guest to stop making SVCs and writes what each of its threads is doing to the log
     * @details A thread inside an SVC is waiting for something (the name of the SVC and its first arguments tell for what), a thread that is not inside one is running guest code without ever calling the kernel, which for a game that shows a black screen usually means it's stuck in a loop
     */
    class StallWatchdog {
      private:
        const DeviceState &state;
        std::atomic<bool> stop{};
        std::thread thread;

        void Run() {
            pthread_setname_np(pthread_self(), "Sky-Watchdog");
            AsyncLogger::UpdateTag();

            u64 lastCount{kernel::svc::SvcCounter.load()};
            u32 stalledSeconds{}, reports{};
            while (!stop.load()) {
                for (int i{}; i < 10 && !stop.load(); i++)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));

                u64 count{kernel::svc::SvcCounter.load()};
                if (count != lastCount) {
                    lastCount = count;
                    stalledSeconds = 0;
                    continue;
                }

                stalledSeconds++;
                if (stalledSeconds >= 8 && (stalledSeconds - 8) % 10 == 0 && reports < 6) {
                    reports++;
                    Report(stalledSeconds);
                }
            }
        }

        /**
         * @brief Asks a thread that is running guest code for its registers (a signal answered by NCE::SampleHandler) and describes where it is
         * @return The description, empty if the thread didn't answer
         */
        std::string SampleThread(kernel::type::KThread &thread) {
            thread.sampledPc.store(0, std::memory_order_release);
            if (pthread_kill(thread.GetHostThread(), SIGUSR2) != 0)
                return {};

            u64 pc{};
            for (int i{}; i < 30 && !(pc = thread.sampledPc.load(std::memory_order_acquire)); i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!pc)
                return "\n    (the thread didn't answer the request for its registers)";

            std::string text{fmt::format("\n    PC 0x{:X} LR 0x{:X} SP 0x{:X} FP 0x{:X}", pc, thread.sampledRegs[30].load(), thread.sampledSp.load(), thread.sampledRegs[29].load())};
            text += fmt::format("\n    X0-X7: 0x{:X} 0x{:X} 0x{:X} 0x{:X} 0x{:X} 0x{:X} 0x{:X} 0x{:X}", thread.sampledRegs[0].load(), thread.sampledRegs[1].load(), thread.sampledRegs[2].load(), thread.sampledRegs[3].load(), thread.sampledRegs[4].load(), thread.sampledRegs[5].load(), thread.sampledRegs[6].load(), thread.sampledRegs[7].load());
            text += "\n    Where: " + state.loader->GetStackTrace(std::vector<void *>{reinterpret_cast<void *>(pc), reinterpret_cast<void *>(thread.sampledRegs[30].load())});

            // The instructions around the PC (only inside the page the PC is in, that page is certainly mapped), enough to tell what the loop is waiting for
            u64 pageStart{pc & ~u64{0xFFF}}, pageEnd{pageStart + 0x1000};
            u64 from{std::max(pageStart, (pc & ~u64{3}) - 24)}, to{std::min(pageEnd, (pc & ~u64{3}) + 40)};
            text += "\n    Code:";
            for (u64 address{from}; address + 4 <= to; address += 4)
                text += fmt::format("{} {:08X}", address == (pc & ~u64{3}) ? " >" : "", *reinterpret_cast<u32 *>(address));
            return text;
        }

        void Report(u32 seconds) {
            if (!state.process)
                return;

            std::string report{fmt::format("The guest has not made any SVC for {} seconds (SVCs so far: {}), threads:", seconds, kernel::svc::SvcCounter.load())};
            auto describe{[](i32 svcId) -> std::string {
                if (svcId < 0 || static_cast<size_t>(svcId) >= kernel::svc::SvcTable.size())
                    return fmt::format("0x{:X}", svcId);
                const char *name{kernel::svc::SvcTable[static_cast<size_t>(svcId)].name};
                return name ? std::string{name} : fmt::format("0x{:X}", svcId);
            }};

            state.process->ForEachThread([&](kernel::type::KThread &thread) {
                i32 current{thread.currentSvc.load()};
                report += fmt::format("\n  Thread #{} (priority {}): ", thread.id, thread.priority.load());
                std::string sample;
                if (current < 0 && thread.GetHostThread())
                    sample = SampleThread(thread);
                report += current >= 0 ? fmt::format("waiting inside SVC {}", describe(current)) : "running guest code without calling the kernel";
                report += fmt::format(", last SVC {} (X0=0x{:X} X1=0x{:X} X2=0x{:X})", describe(thread.lastSvc.load()), thread.lastSvcArgs[0].load(), thread.lastSvcArgs[1].load(), thread.lastSvcArgs[2].load());
                report += sample;
            });
            LOGW("{}", report);
        }

      public:
        StallWatchdog(const DeviceState &state) : state(state), thread(&StallWatchdog::Run, this) {}

        ~StallWatchdog() {
            stop = true;
            if (thread.joinable())
                thread.join();
        }
    };

    void OS::Execute(int romFd, loader::RomFormat romType) {
        auto romFile{std::make_shared<vfs::OsBacking>(romFd)};
        auto keyStore{std::make_shared<crypto::KeyStore>(privateAppFilesPath + "keys/")};

        state.loader = [&]() -> std::shared_ptr<loader::Loader> {
            switch (romType) {
                case loader::RomFormat::NRO:
                    return std::make_shared<loader::NroLoader>(std::move(romFile));
                case loader::RomFormat::NSO:
                    return std::make_shared<loader::NsoLoader>(std::move(romFile));
                case loader::RomFormat::NCA:
                    return std::make_shared<loader::NcaLoader>(std::move(romFile), std::move(keyStore));
                case loader::RomFormat::NSP:
                    return std::make_shared<loader::NspLoader>(romFile, keyStore);
                case loader::RomFormat::XCI:
                    return std::make_shared<loader::XciLoader>(romFile, keyStore);
                default:
                    throw exception("Unsupported ROM extension.");
            }
        }();

        // The update of the game the user installed (from the options of the game) replaces the executables and the RomFS and gives the game its new version
        try {
            state.loader->ApplyUpdate(publicAppFilesPath, keyStore);
        } catch (const std::exception &e) {
            LOGE("Failed to apply the installed update, the game runs without it: {}", e.what());
        }

        state.gpu->Initialise();

        auto &process{state.process};
        process = std::make_shared<kernel::type::KProcess>(state);

        auto entry{state.loader->LoadProcessData(process, state)};
        auto &nacp{state.loader->nacp};
        if (nacp) {
            std::string name{nacp->GetApplicationName(language::ApplicationLanguage::AmericanEnglish)}, publisher{nacp->GetApplicationPublisher(language::ApplicationLanguage::AmericanEnglish)};
            if (name.empty())
                name = nacp->GetApplicationName(nacp->GetFirstSupportedTitleLanguage());
            if (publisher.empty())
                publisher = nacp->GetApplicationPublisher(nacp->GetFirstSupportedTitleLanguage());
            LOGINF(R"(Starting "{}" ({}) v{} by "{}")", name, nacp->GetSaveDataOwnerId(), nacp->GetApplicationVersion(), publisher);
        }

        process->InitializeHeapTls();
        auto thread{process->CreateThread(entry)};
        if (thread) {
            soc::host1x::video::LogFfmpegSupport();

            // Says what every thread is doing when the game stops making SVCs (a frozen game's log otherwise just ends)
            StallWatchdog watchdog{state};

            LOGI("Starting main HOS thread");
            thread->Start(true);
            process->Kill(true, true, true);
        }
    }
}
