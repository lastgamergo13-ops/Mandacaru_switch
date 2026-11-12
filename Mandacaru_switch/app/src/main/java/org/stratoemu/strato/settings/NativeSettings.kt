/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2022 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.settings

import android.app.ActivityManager
import android.content.Context
import android.util.Log
import org.stratoemu.strato.BuildConfig
import org.stratoemu.strato.utils.GpuDriverHelper
import kotlinx.serialization.Serializable

/**
 * The settings that will be passed to libskyline when running and executable
 */
@Serializable
@Suppress("unused")
data class NativeSettings(
    // System
    var isDocked : Boolean,
    var usernameValue : String,
    var profilePictureValue : String,
    var systemLanguage : Int,
    var systemRegion : Int,
    var isInternetEnabled : Boolean,

    // Audio
    var isAudioOutputDisabled : Boolean,

    // GPU
    var gpuDriver : String,
    var gpuDriverLibraryName : String,
    var forceTripleBuffering : Boolean,
    var disableFrameThrottling : Boolean,
    var executorSlotCountScale : Int,
    var executorFlushThreshold : Int,
    var useDirectMemoryImport : Boolean,
    var forceMaxGpuClocks : Boolean,
    var freeGuestTextureMemory : Boolean,
    var disableShaderCache : Boolean,

    // Hacks
    var enableFastGpuReadbackHack : Boolean,
    var enableFastReadbackWrites : Boolean,
    var disableSubgroupShuffle : Boolean,
    var fixBloomEffects : Boolean,

    // Debug
    var logLevel : Int,
    var validationLayer : Boolean
) {
    constructor(context : Context, pref : EmulationSettings) : this(
        pref.isDocked,
        pref.usernameValue,
        pref.profilePictureValue,
        pref.systemLanguage,
        pref.systemRegion,
        pref.isInternetEnabled,
        pref.isAudioOutputDisabled,
        if (pref.gpuDriver == EmulationSettings.SYSTEM_GPU_DRIVER) "" else pref.gpuDriver,
        if (pref.gpuDriver == EmulationSettings.SYSTEM_GPU_DRIVER) "" else GpuDriverHelper.getLibraryName(context, pref.gpuDriver),
        pref.forceTripleBuffering,
        pref.disableFrameThrottling,
        limitExecutorSlotCountScale(context, pref.executorSlotCountScale),
        pref.executorFlushThreshold,
        pref.useDirectMemoryImport,
        pref.forceMaxGpuClocks,
        shouldFreeGuestTextureMemory(context, pref.freeGuestTextureMemory),
        pref.disableShaderCache,
        pref.enableFastGpuReadbackHack,
        pref.enableFastReadbackWrites,
        pref.disableSubgroupShuffle,
        pref.fixBloomEffects,
        pref.logLevel,
        BuildConfig.BUILD_TYPE != "release" && pref.validationLayer
    )

    /**
     * Updates settings in libskyline during emulation
     */
    external fun updateNative()

    companion object {
        private const val TAG = "NativeSettings"

        /**
         * Devices with this much RAM (or less) run out of memory long before the emulated game does, the logs of a Moto G8 (3.6 GiB) showed ~110 MiB still available with ~1.8 GiB of shared memory in use
         */
        private const val LowMemoryDeviceThresholdBytes = 4608L * 1024 * 1024

        /**
         * Every step of the executor slot count scale doubles the number of GPU executions (and the textures/buffers they keep alive) that can be in flight, this is the highest value used on low memory devices
         */
        private const val LowMemoryMaxExecutorSlotCountScale = 3

        fun isLowMemoryDevice(context : Context) : Boolean {
            val memoryInfo = ActivityManager.MemoryInfo()
            (context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager)?.getMemoryInfo(memoryInfo) ?: return false
            return memoryInfo.totalMem in 1..LowMemoryDeviceThresholdBytes
        }

        /**
         * Frees the guest's copy of a texture once it has been uploaded to the GPU, this is what keeps texture heavy games from using the whole RAM of a low memory device (the Android low memory killer closed Pokémon Let's Go Eevee on a Moto G8 with ~1.9 GiB of shared memory in use), so it is forced on there regardless of the setting
         */
        private const val ForceFreeGuestTextureMemoryOnLowMemoryDevices = true

        fun shouldFreeGuestTextureMemory(context : Context, requested : Boolean) : Boolean {
            if (requested || !ForceFreeGuestTextureMemoryOnLowMemoryDevices || !isLowMemoryDevice(context))
                return requested

            Log.i(TAG, "Low memory device, enabling free guest texture memory")
            return true
        }

        /**
         * Caps the executor slot count scale on low memory devices so that they don't start swapping heavily, which is what made the FPS collapse and the RAM usage explode
         */
        fun limitExecutorSlotCountScale(context : Context, requested : Int) : Int {
            if (requested <= LowMemoryMaxExecutorSlotCountScale || !isLowMemoryDevice(context))
                return requested

            Log.i(TAG, "Low memory device, limiting the executor slot count scale from $requested to $LowMemoryMaxExecutorSlotCountScale")
            return LowMemoryMaxExecutorSlotCountScale
        }
    }
}
