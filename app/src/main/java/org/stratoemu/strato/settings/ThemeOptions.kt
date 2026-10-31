/*
 * SPDX-License-Identifier: MPL-2.0
 */

package org.stratoemu.strato.settings

import androidx.appcompat.app.AppCompatDelegate
import org.stratoemu.strato.R

/**
 * The single "Theme" setting holds one option that decides both the color and the light/dark mode:
 * 0 is automatic (the default color following the system), then every color has a light and a dark option (1 and 2 are the default color, 3 and 4 the next one...)
 */
object ThemeOptions {
    /** The theme overlay of every color, in the same order as the `theme_options` array (the first one is the default color) */
    val colorOverlays = intArrayOf(
        R.style.ThemeOverlay_Mandacaru_Yellow,
        R.style.ThemeOverlay_Mandacaru_Red,
        R.style.ThemeOverlay_Mandacaru_Orange,
        R.style.ThemeOverlay_Mandacaru_Green,
        R.style.ThemeOverlay_Mandacaru_Blue,
        R.style.ThemeOverlay_Mandacaru_Indigo,
        R.style.ThemeOverlay_Mandacaru_Violet
    )

    fun nightMode(option : Int) = when {
        option <= 0 -> AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM
        option % 2 == 1 -> AppCompatDelegate.MODE_NIGHT_NO
        else -> AppCompatDelegate.MODE_NIGHT_YES
    }

    fun colorIndex(option : Int) = if (option <= 0) 0 else ((option - 1) / 2).coerceIn(0, colorOverlays.size - 1)
}
