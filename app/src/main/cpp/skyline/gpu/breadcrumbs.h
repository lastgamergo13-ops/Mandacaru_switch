// SPDX-License-Identifier: MPL-2.0

#pragma once

#include <array>
#include <mutex>
#include <string>
#include <chrono>
#include <fstream>
#include <common.h>

namespace skyline::gpu {
    /**
     * @brief A short history of what the GPU was asked to do, written to the log when the GPU hangs or the device is lost
     * @details When the driver loses the device nothing says which command caused it, only that a later submission failed. The last few things that were submitted (render passes, their sizes, pipelines that were compiled, presented frames) usually show what the game was doing
     */
    class Breadcrumbs {
      private:
        static constexpr size_t Capacity{96};

        struct Entry {
            u64 sequence{};
            i64 timeMs{};
            std::string text;
        };

        std::mutex mutex;
        std::array<Entry, Capacity> entries;
        u64 nextSequence{};
        std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};

      public:
        static Breadcrumbs &Get() {
            static Breadcrumbs instance;
            return instance;
        }

        template<typename... Args>
        void Add(fmt::format_string<Args...> format, Args &&... args) {
            std::string text{fmt::format(format, std::forward<Args>(args)...)};
            std::scoped_lock lock{mutex};
            auto &entry{entries[nextSequence % Capacity]};
            entry.sequence = nextSequence++;
            entry.timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            entry.text = std::move(text);
        }

        /**
         * @brief Writes the history, oldest first, to the log
         */
        void Dump(const char *reason) {
            std::scoped_lock lock{mutex};
            LOGE("GPU breadcrumbs ({}): the last {} events before the failure, times are milliseconds since the emulator started", reason, std::min<u64>(nextSequence, Capacity));
            u64 first{nextSequence > Capacity ? nextSequence - Capacity : 0};
            for (u64 sequence{first}; sequence < nextSequence; sequence++) {
                const auto &entry{entries[sequence % Capacity]};
                LOGE("  [{:>8}] #{} {}", entry.timeMs, entry.sequence, entry.text);
            }
        }
    };

    /**
     * @brief Writes how much memory the process uses to the log (a GPU hang after the memory ran out looks like a driver bug otherwise)
     */
    inline void LogProcessMemory(const char *reason) {
        std::ifstream status{"/proc/self/status"};
        std::string line, summary;
        while (std::getline(status, line))
            if (line.rfind("VmRSS", 0) == 0 || line.rfind("VmSwap", 0) == 0 || line.rfind("RssShmem", 0) == 0 || line.rfind("Threads", 0) == 0)
                summary += " " + line + ";";
        std::ifstream meminfo{"/proc/meminfo"};
        while (std::getline(meminfo, line))
            if (line.rfind("MemAvailable", 0) == 0 || line.rfind("MemFree", 0) == 0)
                summary += " " + line + ";";
        LOGE("Process memory ({}):{}", reason, summary);
    }
}
