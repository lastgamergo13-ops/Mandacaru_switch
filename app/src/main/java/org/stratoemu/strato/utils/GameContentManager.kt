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

    fun pickMod() = modPicker.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/vnd.rar", "application/x-rar-compressed", "application/x-rar", "application/x-7z-compressed", "application/octet-stream"))

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

    private fun childDirectory(directory : File, name : String) : File? = directory.listFiles()?.firstOrNull { it.isDirectory && it.name.equals(name, ignoreCase = true) }

    private fun hasModContent(directory : File) = childDirectory(directory, "romfs") != null || childDirectory(directory, "exefs") != null

    private val junkExtensions = setOf("txt", "md", "nfo", "url", "html", "htm", "pdf", "png", "jpg", "jpeg", "gif", "db", "ini", "lnk", "log", "docx", "doc")
    private val junkNames = setOf("thumbs.db", "desktop.ini", ".ds_store")

    private fun isJunk(file : File) = file.name.lowercase() in junkNames || file.name.startsWith("._") || file.extension.lowercase() in junkExtensions

    /** Extracts the archives found inside the extracted files (a mod packed inside another archive), at most [depth] levels */
    private fun extractNestedArchives(directory : File, depth : Int) {
        if (depth <= 0)
            return
        val archives = directory.walkTopDown().filter { it.isFile && (it.extension.lowercase() in ArchiveUtils.archiveExtensions) }.toList()
        for (archive in archives) {
            if (ArchiveUtils.detectFormat(archive) == null)
                continue
            val target = File(archive.parentFile, archive.nameWithoutExtension + "_extracted")
            try {
                ArchiveUtils.extract(archive, target)
                archive.delete()
                extractNestedArchives(target, depth - 1)
            } catch (e : Exception) {
                // An archive that can't be read is skipped, the rest of the mod can still be installed
            }
        }
    }

    private fun copyFlat(files : List<File>, target : File) : Int {
        var count = 0
        target.mkdirs()
        for (file in files) {
            var destination = File(target, file.name)
            var index = 1
            while (destination.exists()) // two patches with the same name in different directories
                destination = File(target, "${file.nameWithoutExtension}_${index++}.${file.extension}")
            file.copyTo(destination)
            count++
        }
        return count
    }

    /**
     * Finds the directories of [directory] that directly contain a `romfs` or `exefs` directory (in any case), without looking inside the ones that were found
     */
    private fun findModRoots(directory : File, depth : Int = 10) : List<File> {
        if (hasModContent(directory))
            return listOf(directory)
        if (depth <= 0)
            return emptyList()
        return directory.listFiles()?.filter { it.isDirectory }?.flatMap { findModRoots(it, depth - 1) } ?: emptyList()
    }

    private fun copyMod(root : File, name : String, extraPatches : List<File>) : Int {
        val safeName = name.replace(Regex("[^A-Za-z0-9 ._()\\[\\]-]"), "_").ifBlank { "mod" }
        val target = File(modsPath, safeName)
        target.deleteRecursively()

        var count = 0
        for (part in arrayOf("romfs", "exefs")) {
            val directory = childDirectory(root, part) ?: continue
            directory.copyRecursively(File(target, part), true)
            count += directory.walkTopDown().count { it.isFile }
        }
        if (extraPatches.isNotEmpty())
            count += copyFlat(extraPatches, File(target, "exefs"))
        return count
    }

    /**
     * Installs everything a translation or mod needs from the extracted archive. All of it is searched, not only the top level:
     *  - the directories with a `romfs`/`exefs` directory inside (in any case, inside a title id directory, `atmosphere/contents`, `load`... or one directory per mod)
     *  - `exefs_patches` directories of Atmosphère and loose `.ips`/`.pchtxt` patches, installed in the `exefs` of the mod
     *  - archives inside the archive
     *  - when there is none of that, the files of the archive are taken as the RomFS files of the game (the directories of the game directly at the top of the archive)
     * @return The number of files that were installed
     */
    private fun installModFiles(source : File, modName : String) : Int {
        extractNestedArchives(source, 2)

        // A directory with the title id of this game limits the search, a pack with the files of several games only installs the one that is being managed
        val scope = source.walkTopDown().maxDepth(8).firstOrNull { it.isDirectory && it.name.equals(gameId, ignoreCase = true) } ?: source

        val roots = findModRoots(scope).filterNot { root -> root.name.equals("exefs_patches", ignoreCase = true) }
        val rootPaths = roots.map { it.canonicalPath }

        // Patches that aren't inside a mod directory
        val patches = scope.walkTopDown().filter { file ->
            file.isFile && (file.extension.equals("ips", ignoreCase = true) || file.extension.equals("pchtxt", ignoreCase = true)) &&
                    rootPaths.none { file.canonicalPath.startsWith(it + File.separator) }
        }.toList()

        var count = 0
        if (roots.isNotEmpty()) {
            roots.forEachIndexed { index, root ->
                // The patches that are next to the only mod belong to it, with several mods they are installed with the first one
                count += copyMod(root, if (roots.size == 1 || root == scope) modName else root.name, if (index == 0) patches else emptyList())
            }
            return count
        }

        if (patches.isNotEmpty()) {
            val target = File(modsPath, modName.replace(Regex("[^A-Za-z0-9 ._()\\[\\]-]"), "_").ifBlank { "mod" })
            target.deleteRecursively()
            count += copyFlat(patches, File(target, "exefs"))
        }

        // Nothing is named romfs/exefs: the archive is the content of the RomFS. The directories that only wrap the content (a single directory and no file) are skipped
        var content = scope
        while (true) {
            val entries = content.listFiles()?.filter { !isJunk(it) } ?: break
            val only = entries.singleOrNull()
            if (only != null && only.isDirectory && !only.name.matches(titleIdRegex)) content = only else break
        }
        val files = content.walkTopDown().filter { it.isFile && !isJunk(it) && !it.name.endsWith(".ips") && !it.name.endsWith(".pchtxt") }.toList()
        if (files.isNotEmpty()) {
            val target = File(modsPath, modName.replace(Regex("[^A-Za-z0-9 ._()\\[\\]-]"), "_").ifBlank { "mod" })
            if (patches.isEmpty())
                target.deleteRecursively()
            val romfs = File(target, "romfs")
            for (file in files) {
                val destination = File(romfs, file.relativeTo(content).path)
                destination.parentFile?.mkdirs()
                file.copyTo(destination, true)
                count++
            }
        }
        return count
    }

    private fun installMod(uri : Uri) {
        val modName = displayName(uri).substringBeforeLast('.')

        val task : () -> Unit = {
            var message = activity.getString(R.string.import_mod_error)

            try {
                tempPath.deleteRecursively()
                tempPath.mkdirs()

                // The archive is copied first, RAR and 7z need to be read in any order
                val archiveFile = File(tempPath, "mod_archive")
                activity.contentResolver.openInputStream(uri)?.use { input -> archiveFile.outputStream().use { input.copyTo(it) } }
                val extracted = File(tempPath, "content")
                ArchiveUtils.extract(archiveFile, extracted)
                archiveFile.delete()

                message = if (installModFiles(extracted, modName) > 0)
                    activity.getString(R.string.import_mod_success)
                else
                    activity.getString(R.string.import_mod_invalid)
            } catch (e : Exception) {
                message = activity.getString(R.string.import_mod_error) + (e.message?.let { ": $it" } ?: "")
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
