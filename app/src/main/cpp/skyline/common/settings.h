// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#pragma once

#include <mutex>
#include "language.h"
#include "logger/logger.h"

namespace skyline {
    /**
     * @brief The Settings class provides a simple interface to access user-defined settings, update values and subscribe callbacks to observe changes.
     */
    /**
     * @brief The graphics API the user asked for, the values match EmulationSettings.GRAPHICS_API_* in the Kotlin side
     */
    enum class GraphicsApi : u32 {
        Automatic = 0, //!< The emulator chooses the API (Vulkan unless the device can't use it)
        Vulkan = 1,
        OpenGL = 2, //!< OpenGL ES (experimental, the backend is still being written, see 'gpu/graphics_api.h')
    };

    class Settings {
        template<typename T>
        class Setting {
            using Callback = std::function<void(const T &)>;
            std::vector<Callback> callbacks; //!< Callbacks to be called when this setting changes
            T value;
            std::mutex valueMutex;
            std::mutex callbackMutex;

            /**
             * @brief Calls all callbacks registered for this setting
             * @note Locking of the setting value must be handled by the caller
             */
            void OnSettingChanged() {
                std::scoped_lock lock{callbackMutex};
                for (const auto &callback : callbacks)
                    callback(value);
            }

          public:
            /**
             * @return The underlying setting value
             */
            const T &operator*() {
                std::scoped_lock lock{valueMutex};
                return value;
            }

            /**
             * @brief Sets the underlying setting value, signalling any callbacks if necessary
             */
            void operator=(T newValue) {
                std::scoped_lock lock{valueMutex};
                if (value != newValue) {
                    value = std::move(newValue);
                    OnSettingChanged();
                }
            }

            /**
             * @brief Register a callback to be run when this setting changes
             */
            void AddCallback(Callback callback) {
                std::scoped_lock lock{callbackMutex};
                callbacks.push_back(std::move(callback));
            }
        };

      public:
        // System
        Setting<bool> isDocked; //!< If the emulated Switch should be handheld or docked
        Setting<std::string> usernameValue; //!< The user name to be supplied to the guest
        Setting<std::string> profilePictureValue; //!< The profile picture path to be supplied to the guest
        Setting<language::SystemLanguage> systemLanguage; //!< The system language
        Setting<region::RegionCode> systemRegion; //!< The system region
        Setting<bool> isInternetEnabled; //!< If emulator uses internet

        // Display
        Setting<bool> forceTripleBuffering; //!< If the presentation engine should always triple buffer even if the swapchain supports double buffering
        Setting<bool> disableFrameThrottling; //!< Allow the guest to submit frames without any blocking calls
        Setting<bool> disableShaderCache;  //!< Prevents cached shaders from being loaded and disables caching of new shaders

        // GPU
        Setting<GraphicsApi> graphicsApi; //!< The graphics API that was asked for
        Setting<std::string> gpuDriver; //!< The label of the GPU driver to use
        Setting<std::string> gpuDriverLibraryName; //!< The name of the GPU driver library to use
        Setting<u32> executorSlotCountScale; //!< Number of GPU executor slots that can be used concurrently
        Setting<u32> executorFlushThreshold; //!< Number of commands that need to accumulate before they're flushed to the GPU
        Setting<bool> useDirectMemoryImport; //!< If buffer emulation should be done by importing guest buffer mappings
        Setting<bool> forceMaxGpuClocks; //!< If the GPU should be forced to run at maximum clocks
        Setting<bool> freeGuestTextureMemory; //!< If guest textrue memory should be freed when the owning texture is GPU dirty
        Setting<u32> stickDeadzonePercent; //!< The radius of the circular deadzone of the sticks in percent of their range (applies to the on-screen sticks and to controllers)
        Setting<u32> gameMemoryLimitMiB; //!< The amount of memory in MiB that games are told they have (a retail console gives an application 3285 MiB), games size their heap and pools from it so a lower value makes them use less RAM

        // Hacks
        Setting<bool> enableFastGpuReadbackHack; //!< If the CPU texture readback skipping hack should be used
        Setting<bool> enableFastReadbackWrites; //!< If buffers should be treated as CPU dirty when written with the readback hack
        Setting<bool> disableSubgroupShuffle; //!< If shader subgroup suffle operations should be ignored
        Setting<bool> fixBloomEffects; //!< If textures sampled with an explicit lod should be darkened slightly to hide the over-bright bloom some games have on some GPUs

        // Audio
        Setting<bool> isAudioOutputDisabled; //!< Disables audio output

        // Debug
        Setting<AsyncLogger::LogLevel> logLevel; //!< The log level
        Setting<bool> validationLayer; //!< If the vulkan validation layer is enabled

        Settings() = default;

        /**
         * @brief Writes every setting and its value to the log (Info), so a log shows how the emulator was configured
         */
        void LogSettings() {
            LOGINF("Settings:");
            LOGINF("  System: docked: {}, language: {}, region: {}, internet: {}", *isDocked, static_cast<u32>(*systemLanguage), static_cast<u32>(*systemRegion), *isInternetEnabled);
            LOGINF("  Display: force triple buffering: {}, disable frame throttling: {}, disable shader cache: {}", *forceTripleBuffering, *disableFrameThrottling, *disableShaderCache);
            LOGINF("  Graphics API: {} (0 = automatic, 1 = Vulkan, 2 = OpenGL ES)", static_cast<u32>(*graphicsApi));
            LOGINF("  GPU: driver: \"{}\", driver library: \"{}\", executor slot count scale: {}, executor flush threshold: {}, direct memory import: {}, force max GPU clocks: {}, free guest texture memory: {}, game memory limit: {} MiB", *gpuDriver, *gpuDriverLibraryName, *executorSlotCountScale, *executorFlushThreshold, *useDirectMemoryImport, *forceMaxGpuClocks, *freeGuestTextureMemory, *gameMemoryLimitMiB);
            LOGINF("  Hacks: fast GPU readback: {}, fast readback writes: {}, disable subgroup shuffle: {}, fix bloom effects: {}", *enableFastGpuReadbackHack, *enableFastReadbackWrites, *disableSubgroupShuffle, *fixBloomEffects);
            LOGINF("  Audio: output disabled: {}", *isAudioOutputDisabled);
            LOGINF("  Debug: log level: {}, validation layer: {}", static_cast<u32>(*logLevel), *validationLayer);
        }

        virtual ~Settings() = default;

        /**
         * @brief Updates settings with the given values
         * @note This method is platform-specific and must be overridden
         */
        virtual void Update() = 0;
    };
}
