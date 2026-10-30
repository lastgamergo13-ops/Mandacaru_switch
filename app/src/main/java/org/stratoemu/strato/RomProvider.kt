package org.stratoemu.strato

import android.annotation.SuppressLint
import android.content.Context
import android.net.Uri
import android.util.Log
import androidx.documentfile.provider.DocumentFile
import dagger.hilt.android.qualifiers.ApplicationContext
import org.stratoemu.strato.loader.AppEntry
import org.stratoemu.strato.loader.RomFile
import org.stratoemu.strato.loader.RomFormat
import org.stratoemu.strato.loader.RomFormat.*
import javax.inject.Inject
import javax.inject.Singleton

@Singleton
class RomProvider @Inject constructor(@ApplicationContext private val context : Context) {
    /**
     * This adds all files in [directory] with [extension] as an entry using [RomFile] to load metadata
     */
    @SuppressLint("DefaultLocale")
    private fun addEntries(fileFormats : Map<String, RomFormat>, directory : DocumentFile, entries : ArrayList<AppEntry>, systemLanguage : Int) {
        directory.listFiles().forEach { file ->
            if (file.isDirectory) {
                addEntries(fileFormats, file, entries, systemLanguage)
            } else {
                fileFormats[file.name?.substringAfterLast(".")?.lowercase()]?.let { romFormat->
                    // A single file that can't be opened or parsed must not make the whole library empty, it's skipped and the rest are still listed
                    try {
                        entries.add(RomFile(context, romFormat, file.uri, systemLanguage).appEntry)
                    } catch (e : Exception) {
                        Log.w("RomProvider", "Skipping ${file.name}: ${e.message}")
                    }
                }
            }
        }
    }

    fun loadRoms(searchLocation : Uri, systemLanguage : Int) = DocumentFile.fromTreeUri(context, searchLocation)!!.let { documentFile ->
        arrayListOf<AppEntry>().apply {
            addEntries(mapOf("nro" to NRO, "nso" to NSO, "nca" to NCA, "nsp" to NSP, "xci" to XCI), documentFile, this, systemLanguage)
        }
    }
}
