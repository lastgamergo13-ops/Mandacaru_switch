/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.preference

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.AttributeSet
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.documentfile.provider.DocumentFile
import androidx.preference.Preference
import androidx.preference.Preference.SummaryProvider
import androidx.preference.R
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.stratoemu.strato.di.getSettings
import org.stratoemu.strato.utils.registerLauncher

/**
 * The folders the games are searched in: any number of them, on the internal storage, an SD card or another storage the system file picker shows
 */
class FolderPickerPreference @JvmOverloads constructor(context : Context, attrs : AttributeSet? = null, defStyleAttr : Int = R.attr.preferenceStyle) : Preference(context, attrs, defStyleAttr) {
    private val documentPicker = (context as ComponentActivity).registerLauncher(ActivityResultContracts.OpenDocumentTree()) {
        it?.let { uri ->
            context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)

            context.getSettings().apply {
                addSearchLocation(uri)
                refreshRequired = true
            }
            notifyChanged()
        }
    }

    private fun displayName(uri : Uri) : String = DocumentFile.fromTreeUri(context, uri)?.name?.takeIf { it.isNotBlank() } ?: Uri.decode(uri.lastPathSegment ?: uri.toString())

    init {
        summaryProvider = SummaryProvider<FolderPickerPreference> {
            val locations = context.getSettings().searchLocations
            if (locations.isEmpty())
                context.getString(org.stratoemu.strato.R.string.search_location_none)
            else
                locations.joinToString("\n") { Uri.decode(it.lastPathSegment ?: it.toString()) }
        }
    }

    /** Shows the folders with the option to add another one, tapping a folder offers to remove it */
    override fun onClick() {
        val settings = context.getSettings()
        val locations = settings.searchLocations

        val dialog = MaterialAlertDialogBuilder(context)
            .setTitle(org.stratoemu.strato.R.string.search_location)
            .setPositiveButton(org.stratoemu.strato.R.string.search_location_add) { _, _ -> pick() }
            .setNegativeButton(org.stratoemu.strato.R.string.search_location_close, null)

        if (locations.isEmpty()) {
            dialog.setMessage(org.stratoemu.strato.R.string.search_location_none)
        } else {
            dialog.setItems(locations.map { displayName(it) }.toTypedArray()) { _, which ->
                val location = locations[which]
                MaterialAlertDialogBuilder(context)
                    .setTitle(org.stratoemu.strato.R.string.search_location_remove_title)
                    .setMessage(displayName(location))
                    .setPositiveButton(org.stratoemu.strato.R.string.search_location_remove) { _, _ ->
                        settings.removeSearchLocation(location)
                        settings.refreshRequired = true
                        notifyChanged()
                    }
                    .setNegativeButton(android.R.string.cancel, null)
                    .show()
            }
        }
        dialog.show()
    }

    /** Opens the folder picker to add a folder, used by the first-run setup that has its own buttons */
    fun pick() = documentPicker.launch(null)
}
