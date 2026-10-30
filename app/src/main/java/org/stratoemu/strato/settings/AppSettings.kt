/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.settings

import android.content.Context
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
    var searchLocation by sharedPreferences(context, "")

    // Appearance
    var appTheme by sharedPreferences(context, 2)
    var useMaterialYou by sharedPreferences(context, false)
    var themeColor by sharedPreferences(context, 2) // The index of the color of the theme, 2 (yellow) is the default theme
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
