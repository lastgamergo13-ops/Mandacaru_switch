/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.preference

import android.content.Context
import org.stratoemu.strato.utils.registerLauncher
import android.provider.OpenableColumns
import android.util.AttributeSet
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.preference.Preference
import androidx.preference.Preference.SummaryProvider
import com.google.android.material.snackbar.Snackbar
import org.stratoemu.strato.R
import org.stratoemu.strato.getPublicFilesDir
import org.stratoemu.strato.settings.SettingsActivity
import java.io.File

/**
 * This preference imports code patches (.pchtxt and .ips) that fix games or install translations that are distributed as patches,
 * they're applied to the game's executables the next time the game is started
 */
class PatchImportPreference @JvmOverloads constructor(context : Context, attrs : AttributeSet? = null, defStyleAttr : Int = androidx.preference.R.attr.preferenceStyle) : Preference(context, attrs, defStyleAttr) {
    private val patchesPath = File(context.getPublicFilesDir().canonicalPath + "/patches/")

    private val documentPicker = (context as ComponentActivity).registerLauncher(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        var installed = 0

        patchesPath.mkdirs()
        uris.forEach { uri ->
            val name = context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) cursor.getString(0) else null
            } ?: return@forEach

            val extension = name.substringAfterLast('.', "").lowercase()
            if (extension != "pchtxt" && extension != "ips")
                return@forEach

            try {
                // File(name).name drops any directory components the provider may have put in the name
                val target = File(patchesPath, File(name).name)
                context.contentResolver.openInputStream(uri)?.use { input ->
                    target.outputStream().use { output -> input.copyTo(output) }
                    installed++
                }
            } catch (e : Exception) {
                // A file that can't be copied is skipped, the result message shows how many were installed
            }
        }

        val message = if (installed > 0)
            context.getString(R.string.import_patches_success, installed)
        else
            context.getString(R.string.import_patches_none)
        Snackbar.make((context as SettingsActivity).binding.root, message, Snackbar.LENGTH_LONG).show()

        notifyChanged()
    }

    init {
        summaryProvider = SummaryProvider<Preference> {
            val count = patchesPath.listFiles()?.count { it.isFile } ?: 0
            context.getString(R.string.patches_installed, count)
        }
    }

    override fun onClick() = documentPicker.launch(arrayOf("*/*"))
}
