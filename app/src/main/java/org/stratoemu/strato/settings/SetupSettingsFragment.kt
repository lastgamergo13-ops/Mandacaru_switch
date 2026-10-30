/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.settings

import android.os.Bundle
import android.view.View
import androidx.core.view.updatePadding
import androidx.preference.PreferenceFragmentCompat
import org.stratoemu.strato.R

/**
 * The intent extra that opens the settings in the first-run setup mode
 */
const val SetupModeTag = "setup_mode"

/**
 * The first-run setup shows only what is needed to start playing: the keys, the firmware and the folder of the games
 */
class SetupSettingsFragment : PreferenceFragmentCompat() {
    override fun onCreatePreferences(savedInstanceState : Bundle?, rootKey : String?) {
        setPreferencesFromResource(R.xml.setup_preferences, rootKey)
    }

    override fun onViewCreated(view : View, savedInstanceState : Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        // Leaves room for the Skip and Done buttons at the bottom of the screen
        listView.clipToPadding = false
        listView.updatePadding(bottom = (96 * resources.displayMetrics.density).toInt())
    }
}
