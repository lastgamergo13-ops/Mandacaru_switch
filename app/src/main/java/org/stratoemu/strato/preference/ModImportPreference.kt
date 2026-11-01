/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.preference

import android.content.Context
import org.stratoemu.strato.utils.registerLauncher
import android.net.Uri
import android.provider.OpenableColumns
import android.text.InputType
import android.util.AttributeSet
import android.widget.EditText
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.preference.Preference
import androidx.preference.Preference.SummaryProvider
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.snackbar.Snackbar
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import org.stratoemu.strato.R
import org.stratoemu.strato.fragments.IndeterminateProgressDialogFragment
import org.stratoemu.strato.getPublicFilesDir
import org.stratoemu.strato.settings.SettingsActivity
import org.stratoemu.strato.utils.ZipUtils
import java.io.File

/**
 * This preference installs mods from a ZIP, a mod is a directory with a `romfs` directory (replaces files of the game, used for dubbing and translations) and/or an `exefs` directory (code patches).
 * The ZIP should have a directory named with the game's title id, if it doesn't the user is asked for it.
 */
class ModImportPreference @JvmOverloads constructor(context : Context, attrs : AttributeSet? = null, defStyleAttr : Int = androidx.preference.R.attr.preferenceStyle) : Preference(context, attrs, defStyleAttr) {
    private val modsPath = File(context.getPublicFilesDir().canonicalPath + "/mods/")
    private val tempPath = File(context.cacheDir, "mod_import")
    private val titleIdRegex = Regex("[0-9A-Fa-f]{16}")

    private val documentPicker = (context as ComponentActivity).registerLauncher(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null)
            startInstall(uri)
    }

    init {
        summaryProvider = SummaryProvider<Preference> {
            val count = modsPath.listFiles()?.sumOf { game -> game.listFiles()?.count { it.isDirectory } ?: 0 } ?: 0
            context.getString(R.string.mods_installed, count)
        }
    }

    override fun onClick() = documentPicker.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))

    private fun displayName(uri : Uri) : String {
        val name = context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0) else null
        }
        return (name ?: "mod").substringBeforeLast('.').replace(Regex("[^A-Za-z0-9 ._()\\[\\]-]"), "_").ifBlank { "mod" }
    }

    /**
     * @return The directory that directly contains the mod's `romfs` or `exefs` directory, null if there isn't one
     */
    private fun findModRoot(directory : File) : File? {
        return directory.walkTopDown().maxDepth(6).firstOrNull {
            it.isDirectory && (File(it, "romfs").isDirectory || File(it, "exefs").isDirectory)
        }
    }

    /**
     * @return The directory named with a title id inside the unzipped mod, null if there isn't one
     */
    private fun findTitleDirectory(directory : File) : File? {
        return directory.walkTopDown().maxDepth(6).firstOrNull { it.isDirectory && it.name.matches(titleIdRegex) }
    }

    /**
     * Copies the mod found in [source] to the mods directory of the game
     * @return The number of files that were installed
     */
    private fun installMod(source : File, titleId : String, name : String) : Int {
        val modRoot = findModRoot(source) ?: return 0
        val target = File(modsPath, titleId.uppercase() + "/" + name)
        target.deleteRecursively()

        var count = 0
        for (part in arrayOf("romfs", "exefs")) {
            val directory = File(modRoot, part)
            if (directory.isDirectory) {
                directory.copyRecursively(File(target, part), true)
                count += directory.walkTopDown().count { it.isFile }
            }
        }
        return count
    }

    /**
     * Installs the mods of a directory that is named with a title id, it can have a mod directly inside or several mods in directories of their own
     */
    private fun installFromTitleDirectory(titleDirectory : File, name : String) : Int {
        val titleId = titleDirectory.name
        if (File(titleDirectory, "romfs").isDirectory || File(titleDirectory, "exefs").isDirectory)
            return installMod(titleDirectory, titleId, name)

        var count = 0
        titleDirectory.listFiles()?.filter { it.isDirectory }?.forEach { count += installMod(it, titleId, it.name) }
        return count
    }

    private fun showResult(message : Int) {
        CoroutineScope(Dispatchers.Main).launch {
            Snackbar.make((context as SettingsActivity).binding.root, message, Snackbar.LENGTH_LONG).show()
            notifyChanged()
        }
    }

    private fun startInstall(uri : Uri) {
        val name = displayName(uri)

        val task : () -> Unit = {
            var message = R.string.import_mod_error
            var needsTitleId = false

            try {
                tempPath.deleteRecursively()
                tempPath.mkdirs()
                context.contentResolver.openInputStream(uri)?.use { ZipUtils.unzip(it, tempPath) }

                val titleDirectory = findTitleDirectory(tempPath)
                if (titleDirectory != null) {
                    message = if (installFromTitleDirectory(titleDirectory, name) > 0) R.string.import_mod_success else R.string.import_mod_invalid
                    tempPath.deleteRecursively()
                } else if (findModRoot(tempPath) != null) {
                    needsTitleId = true // The unzipped files are kept for when the user tells us the game
                } else {
                    message = R.string.import_mod_invalid
                    tempPath.deleteRecursively()
                }
            } catch (e : Exception) {
                tempPath.deleteRecursively()
            }

            if (needsTitleId)
                CoroutineScope(Dispatchers.Main).launch { askTitleId(name) }
            else
                showResult(message)
        }

        val activity = context as SettingsActivity
        IndeterminateProgressDialogFragment.newInstance(activity, R.string.import_mod_in_progress, task)
            .show(activity.supportFragmentManager, IndeterminateProgressDialogFragment.TAG)
    }

    private fun askTitleId(name : String) {
        val input = EditText(context).apply {
            inputType = InputType.TYPE_CLASS_TEXT
            hint = "0100XXXXXXXXXXXX"
        }

        MaterialAlertDialogBuilder(context)
            .setTitle(R.string.mod_title_id_title)
            .setMessage(R.string.mod_title_id_message)
            .setView(input)
            .setPositiveButton(android.R.string.ok) { _, _ ->
                val titleId = input.text.toString().trim()
                if (titleId.matches(titleIdRegex)) {
                    val task : () -> Unit = {
                        val count = try {
                            installMod(tempPath, titleId, name)
                        } catch (e : Exception) {
                            0
                        }
                        tempPath.deleteRecursively()
                        showResult(if (count > 0) R.string.import_mod_success else R.string.import_mod_error)
                    }

                    val activity = context as SettingsActivity
                    IndeterminateProgressDialogFragment.newInstance(activity, R.string.import_mod_in_progress, task)
                        .show(activity.supportFragmentManager, IndeterminateProgressDialogFragment.TAG)
                } else {
                    tempPath.deleteRecursively()
                    showResult(R.string.mod_title_id_invalid)
                }
            }
            .setNegativeButton(android.R.string.cancel) { _, _ -> tempPath.deleteRecursively() }
            .setOnCancelListener { tempPath.deleteRecursively() }
            .show()
    }
}
