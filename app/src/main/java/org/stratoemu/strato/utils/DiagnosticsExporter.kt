/*
 * SPDX-License-Identifier: MPL-2.0
 */

package org.stratoemu.strato.utils

import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import org.stratoemu.strato.getPublicFilesDir
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Copies the files that are useful for diagnosing a problem (the emulator logs, the reason the previous run of the app ended and the SPIR-V of the shaders the driver refused to compile)
 * to a folder chosen by the user through the system folder picker. Android doesn't let the user browse `Android/data` anymore, so the files can't be taken from where they are written.
 */
object DiagnosticsExporter {
    private const val TAG = "DiagnosticsExporter"

    private class Entry(val name : String, val file : File)

    class Result(val copied : Int, val failed : Int, val folderName : String?)

    /**
     * @return The files that would be exported, in the order they are copied (with the name each one gets in the destination)
     */
    private fun collect(context : Context) : List<Entry> {
        val root = context.getPublicFilesDir()
        val entries = mutableListOf<Entry>()

        root.resolve("logs").listFiles { file -> file.isFile && file.length() > 0 }?.sortedBy { it.name }?.forEach { entries += Entry(it.name, it) }

        root.resolve("last_exit_reasons.txt").takeIf { it.isFile && it.length() > 0 }?.let { entries += Entry(it.name, it) }

        // shader_dumps/<titleId>/failed/<module>_<shader hash>.spv, the title id is added to the name so that files of different games don't collide
        root.resolve("shader_dumps").listFiles { file -> file.isDirectory }?.sortedBy { it.name }?.forEach { titleDirectory ->
            titleDirectory.resolve("failed").listFiles { file -> file.isFile && file.length() > 0 }?.sortedBy { it.name }?.forEach {
                entries += Entry("${titleDirectory.name}_${it.name}", it)
            }
        }

        return entries
    }

    fun hasFiles(context : Context) : Boolean = collect(context).isNotEmpty()

    /**
     * Copies the files to a new folder (named after the current date and time) created inside the folder the user picked
     * @note This does file I/O, it must not be called from the main thread
     */
    fun export(context : Context, treeUri : Uri) : Result {
        val entries = collect(context)
        if (entries.isEmpty())
            return Result(0, 0, null)

        val tree = DocumentFile.fromTreeUri(context, treeUri)
        val folderName = "Mandacaru_diagnostics_${SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())}"
        val folder = try {
            tree?.createDirectory(folderName)
        } catch (e : Exception) {
            Log.w(TAG, "Couldn't create the destination folder", e)
            null
        } ?: return Result(0, entries.size, null)

        var copied = 0
        var failed = 0
        for (entry in entries) {
            try {
                // A generic type is used for everything: with a specific one some providers add an extension to the name (emulation.log.txt)
                val target = folder.createFile("application/octet-stream", entry.name) ?: throw IllegalStateException("The file couldn't be created")
                val output = context.contentResolver.openOutputStream(target.uri) ?: throw IllegalStateException("The file couldn't be opened")
                output.use { stream -> entry.file.inputStream().use { it.copyTo(stream) } }
                copied++
            } catch (e : Exception) {
                Log.w(TAG, "Couldn't export '${entry.file}'", e)
                failed++
            }
        }

        return Result(copied, failed, folderName)
    }
}
