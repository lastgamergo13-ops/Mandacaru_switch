/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato

import android.app.Activity
import android.app.Application
import android.content.Context
import android.os.Bundle
import com.google.android.material.color.DynamicColors
import com.google.android.material.color.DynamicColorsOptions
import dagger.hilt.android.HiltAndroidApp
import org.stratoemu.strato.di.getSettings
import java.io.File

/**
 * @return The optimal directory for putting public files inside, this may return a private directory if a public directory cannot be retrieved
 */
fun Context.getPublicFilesDir() : File = getExternalFilesDir(null) ?: filesDir

@HiltAndroidApp
class StratoApplication : Application() {
    init {
        instance = this
    }

    companion object {
        lateinit var instance : StratoApplication
            private set

        val context : Context get() = instance.applicationContext
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        System.loadLibrary("skyline")

        val dynamicColorsOptions = DynamicColorsOptions.Builder().setPrecondition { _, _ -> getSettings().useMaterialYou }.build()
        DynamicColors.applyToActivitiesIfAvailable(this, dynamicColorsOptions)

        // The color the user chose is applied as a theme overlay to every screen before it is created, the default color (yellow) and Material You don't need it
        val colorOverlays = intArrayOf(
            R.style.ThemeOverlay_Mandacaru_Red,
            R.style.ThemeOverlay_Mandacaru_Orange,
            0,
            R.style.ThemeOverlay_Mandacaru_Green,
            R.style.ThemeOverlay_Mandacaru_Blue,
            R.style.ThemeOverlay_Mandacaru_Indigo,
            R.style.ThemeOverlay_Mandacaru_Violet
        )
        registerActivityLifecycleCallbacks(object : ActivityLifecycleCallbacks {
            override fun onActivityPreCreated(activity : Activity, savedInstanceState : Bundle?) {
                val settings = getSettings()
                val overlay = colorOverlays.getOrElse(settings.themeColor) { 0 }
                if (!settings.useMaterialYou && overlay != 0)
                    activity.theme.applyStyle(overlay, true)
            }

            override fun onActivityCreated(activity : Activity, savedInstanceState : Bundle?) {}
            override fun onActivityStarted(activity : Activity) {}
            override fun onActivityResumed(activity : Activity) {}
            override fun onActivityPaused(activity : Activity) {}
            override fun onActivityStopped(activity : Activity) {}
            override fun onActivitySaveInstanceState(activity : Activity, outState : Bundle) {}
            override fun onActivityDestroyed(activity : Activity) {}
        })
    }
}
