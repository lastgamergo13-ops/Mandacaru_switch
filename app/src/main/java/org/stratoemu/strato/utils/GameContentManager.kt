/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.utils

import android.net.Uri
import android.provider.OpenableColumns
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import org.stratoemu.strato.R
import org.stratoemu.strato.fragments.IndeterminateProgressDialogFragment
import org.stratoemu.strato.getPublicFilesDir
import java.io.File

/**
 * Manages the content that the user adds to ONE game (DLC/updates, mods and fix patches), everything is stored in directories named with the game's title id so it never affects other games
 * @param titleId The title id of the game
 * @param onChanged Called on the main thread after something was installed or removed
 */
class GameContentManager(private val activity : AppCompatActivity, titleId : String, private val onChanged : () -> Unit) {
    private val publicFiles = activity.getPublicFilesDir().canonicalPath
    private val gameId = titleId.uppercase()

    /** The NCAs and the title keys of the DLC (read by the emulator when the game runs) */
    private val dlcPath = File("$publicFiles/games/$gameId/dlc/registered")
    private val titleKeysPath = File("$publicFiles/games/$gameId/dlc/title_keys.txt")

    /** The mods of the game, each one is a directory with a `romfs` and/or `exefs` directory inside */
    private val modsPath = File("$publicFiles/mods/$gameId")
    private val patchesModName = "Fix patches"

    private val tempPath = File(activity.cacheDir, "mod_import")
    private val titleIdRegex = Regex("[0-9A-Fa-f]{16}")

    // These are registered in the registry directly because this class is created while the screen is already resumed (when the game dialog opens), activity.registerForActivityResult() throws IllegalStateException in that state
    private val dlcPicker = activity.activityResultRegistry.register("game_content_dlc_picker", ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty())
            installDlc(uris)
    }

    private val modPicker = activity.activityResultRegistry.register("game_content_mod_picker", ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null)
            installMod(uri)
    }

    private val patchPicker = activity.activityResultRegistry.register("game_content_patch_picker", ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty())
            installPatches(uris)
    }

    fun pickDlc() = dlcPicker.launch(arrayOf("*/*"))

    fun pickMod() = modPicker.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))

    fun pickPatches() = patchPicker.launch(arrayOf("*/*"))

    fun dlcCount() : Int = dlcPath.listFiles()?.count { it.isFile && it.extension.equals("nca", ignoreCase = true) } ?: 0

    fun modCount() : Int = modsPath.listFiles()?.count { it.isDirectory && it.name != patchesModName } ?: 0

    fun patchCount() : Int = File(modsPath, "$patchesModName/exefs").listFiles()?.count { it.isFile } ?: 0

    fun clearDlc() {
        File("$publicFiles/games/$gameId/dlc").deleteRecursively()
        onChanged()
    }

    fun clearMods() {
        modsPath.listFiles()?.filter { it.isDirectory && it.name != patchesModName }?.forEach { it.deleteRecursively() }
        onChanged()
    }

    fun clearPatches() {
        File(modsPath, patchesModName).deleteRecursively()
        onChanged()
    }

    private fun showMessage(message : String) {
        CoroutineScope(Dispatchers.Main).launch {
            Toast.makeText(activity, message, Toast.LENGTH_LONG).show()
            onChanged()
        }
    }

    private fun runWithProgress(titleRes : Int, task : () -> Unit) {
        IndeterminateProgressDialogFragment.newInstance(activity, titleRes, task)
            .show(activity.supportFragmentManager, IndeterminateProgressDialogFragment.TAG)
    }

    private fun displayName(uri : Uri) : String {
        val name = activity.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0) else null
        }
        return name ?: "file"
    }

    private fun installDlc(uris : List<Uri>) {
        val task : () -> Unit = {
            var installed = 0
            var failed = 0

            uris.forEach { uri ->
                try {
                    val descriptor = activity.contentResolver.openFileDescriptor(uri, "r")
                    if (descriptor == null) {
                        failed++
                    } else {
                        // The native code takes ownership of the descriptor and closes it, this avoids copying a file that can be several GB to the cache first
                        val count = installNsp(descriptor.detachFd(), dlcPath.path, titleKeysPath.path)
                        if (count >= 0)
                            installed += count
                        else
                            failed++
                    }
                } catch (e : Exception) {
                    failed++
                }
            }

            showMessage(activity.getString(R.string.install_content_result, installed, failed))
        }

        runWithProgress(R.string.install_content_in_progress, task)
    }

    private fun installPatches(uris : List<Uri>) {
        val task : () -> Unit = {
            var installed = 0
            val target = File(modsPath, "$patchesModName/exefs")
            target.mkdirs()

            uris.forEach { uri ->
                val name = displayName(uri)
                val extension = name.substringAfterLast('.', "").lowercase()
                if (extension == "pchtxt" || extension == "ips") {
                    try {
                        // File(name).name drops any directory components the provider may have put in the name
                        activity.contentResolver.openInputStream(uri)?.use { input ->
                            File(target, File(name).name).outputStream().use { output -> input.copyTo(output) }
                            installed++
                        }
                    } catch (e : Exception) {
                        // A file that can't be copied is skipped, the message shows how many were installed
                    }
                }
            }

            showMessage(if (installed > 0) activity.getString(R.string.import_patches_success, installed) else activity.getString(R.string.import_patches_none))
        }

        runWithProgress(R.string.install_content_in_progress, task)
    }

    private fun hasModContent(directory : File) = File(directory, "romfs").isDirectory || File(directory, "exefs").isDirectory

    /**
     * @return The directory that directly contains the mod's `romfs` or `exefs` directory, null if there isn't one
     */
    private fun findModRoot(directory : File) : File? {
        return directory.walkTopDown().maxDepth(6).firstOrNull { it.isDirectory && hasModContent(it) }
    }

    private fun copyMod(root : File, name : String) : Int {
        val safeName = name.replace(Regex("[^A-Za-z0-9 ._()\\[\\]-]"), "_").ifBlank { "mod" }
        val target = File(modsPath, safeName)
        target.deleteRecursively()

        var count = 0
        for (part in arrayOf("romfs", "exefs")) {
            val directory = File(root, part)
            if (directory.isDirectory) {
                directory.copyRecursively(File(target, part), true)
                count += directory.walkTopDown().count { it.isFile }
            }
        }
        return count
    }

    /**
     * Installs the mod(s) inside an unzipped mod, it can have a directory named with a title id around it and it can have several mods in directories of their own
     * @return The number of files that were installed
     */
    private fun installModFiles(source : File, zipName : String) : Int {
        val start = source.walkTopDown().maxDepth(6).firstOrNull { it.isDirectory && it.name.matches(titleIdRegex) } ?: source
        if (hasModContent(start))
            return copyMod(start, zipName)

        var count = 0
        start.listFiles()?.filter { it.isDirectory }?.forEach { child ->
            val root = findModRoot(child)
            if (root != null)
                count += copyMod(root, child.name)
        }

        if (count == 0) {
            val root = findModRoot(start)
            if (root != null)
                count = copyMod(root, zipName)
        }
        return count
    }

    private fun installMod(uri : Uri) {
        val zipName = displayName(uri).substringBeforeLast('.')

        val task : () -> Unit = {
            var message = activity.getString(R.string.import_mod_error)

            try {
                tempPath.deleteRecursively()
                tempPath.mkdirs()
                activity.contentResolver.openInputStream(uri)?.use { ZipUtils.unzip(it, tempPath) }

                message = if (installModFiles(tempPath, zipName) > 0)
                    activity.getString(R.string.import_mod_success)
                else
                    activity.getString(R.string.import_mod_invalid)
            } catch (e : Exception) {
                // The error message is shown
            } finally {
                tempPath.deleteRecursively()
            }

            showMessage(message)
        }

        runWithProgress(R.string.import_mod_in_progress, task)
    }

    /**
     * Copies the NCAs of the NSP to [contentsPath] and the title keys of its tickets to [titleKeysPath]
     * @param fd The file descriptor of the NSP, it is closed by this function
     * @return The number of NCAs that were copied or -1 if the NSP couldn't be read
     */
    private external fun installNsp(fd : Int, contentsPath : String, titleKeysPath : String) : Int
}
