/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato

import android.app.Activity
import android.app.Application
import android.content.Context
import android.os.Bundle
import androidx.appcompat.app.AppCompatDelegate
import com.google.android.material.color.DynamicColors
import com.google.android.material.color.DynamicColorsOptions
import dagger.hilt.android.HiltAndroidApp
import org.stratoemu.strato.di.getSettings
import org.stratoemu.strato.settings.ThemeOptions
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

        // The light/dark mode is part of the same setting as the color so every screen (also the ones opened without going through the main screen, like a game shortcut) starts with it
        AppCompatDelegate.setDefaultNightMode(getSettings().appTheme)

        // The color the user chose is applied as a theme overlay to every screen before it is created (Material You uses the colors of the system instead)
        registerActivityLifecycleCallbacks(object : ActivityLifecycleCallbacks {
            override fun onActivityPreCreated(activity : Activity, savedInstanceState : Bundle?) {
                val settings = getSettings()
                if (!settings.useMaterialYou)
                    activity.theme.applyStyle(ThemeOptions.colorOverlays[settings.themeColor], true)
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
