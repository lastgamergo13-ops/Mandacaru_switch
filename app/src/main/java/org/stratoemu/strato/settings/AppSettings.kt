/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.settings

import android.content.Context
import android.net.Uri
import dagger.hilt.android.qualifiers.ApplicationContext
import org.stratoemu.strato.utils.sharedPreferences
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Settings used by the app globally
 */
@Singleton
class AppSettings @Inject constructor(@ApplicationContext private val context : Context) {
    // The first-run setup is shown until the user finishes or skips it
    var setupCompleted by sharedPreferences(context, false)

    // Content
    var searchLocation by sharedPreferences(context, "") // The folders the games are searched in, one per line (a value saved by an older version holds a single folder)

    /** The folders the games are searched in, they can be on the internal storage, on an SD card or on any other storage the system file picker shows */
    val searchLocations : List<Uri> get() = searchLocation.split('\n').map { it.trim() }.filter { it.isNotEmpty() }.map { Uri.parse(it) }

    fun addSearchLocation(uri : Uri) {
        val locations = searchLocations
        if (locations.none { it.toString() == uri.toString() })
            searchLocation = (locations + uri).joinToString("\n")
    }

    fun removeSearchLocation(uri : Uri) {
        searchLocation = searchLocations.filter { it.toString() != uri.toString() }.joinToString("\n")
    }

    // Appearance
    var themeOption by sharedPreferences(context, 0) // One setting for the color and the light/dark mode, see ThemeOptions
    val appTheme get() = ThemeOptions.nightMode(themeOption)
    var useMaterialYou by sharedPreferences(context, false)
    val themeColor get() = ThemeOptions.colorIndex(themeOption)
    var layoutType by sharedPreferences(context, 1)
    var sortAppsBy by sharedPreferences(context, 0)
    var selectAction by sharedPreferences(context, false)
    var filterInvalidFiles by sharedPreferences(context, false)

    // Input
    var onScreenControl by sharedPreferences(context, true)
    var onScreenControlFeedback by sharedPreferences(context, true)
    var onScreenControlRecenterSticks by sharedPreferences(context, true)
    var onScreenControlSnapToGrid by sharedPreferences(context, false)
    var onScreenControlUseStickRegions by sharedPreferences(context, false)

    // Other
    var refreshRequired by sharedPreferences(context, false)
}
