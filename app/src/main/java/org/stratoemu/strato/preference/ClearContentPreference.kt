/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.preference

import android.content.Context
import android.util.AttributeSet
import androidx.preference.Preference
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.snackbar.Snackbar
import org.stratoemu.strato.R
import org.stratoemu.strato.getPublicFilesDir
import org.stratoemu.strato.settings.SettingsActivity
import java.io.File

/**
 * This preference removes everything installed in one of the sections (DLC/updates, mods or translation patches), which one is decided by its key
 */
class ClearContentPreference @JvmOverloads constructor(context : Context, attrs : AttributeSet? = null, defStyleAttr : Int = androidx.preference.R.attr.preferenceStyle) : Preference(context, attrs, defStyleAttr) {
    private fun targets() : List<File> {
        val publicFiles = context.getPublicFilesDir().canonicalPath
        return when (key) {
            "clear_dlc" -> listOf(File("$publicFiles/switch/nand/user/Contents/registered"), File("$publicFiles/switch/nand/user/Contents/title_keys.txt"))
            "clear_mods" -> listOf(File("$publicFiles/mods"))
            "clear_patches" -> listOf(File("$publicFiles/patches"))
            else -> emptyList()
        }
    }

    override fun onClick() {
        MaterialAlertDialogBuilder(context)
            .setTitle(title)
            .setMessage(R.string.clear_content_confirm)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                targets().forEach { it.deleteRecursively() }
                Snackbar.make((context as SettingsActivity).binding.root, R.string.clear_content_done, Snackbar.LENGTH_LONG).show()
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }
}
