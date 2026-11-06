/*
 * SPDX-License-Identifier: MPL-2.0
 */

package org.stratoemu.strato.utils

import net.sf.sevenzipjbinding.ExtractAskMode
import net.sf.sevenzipjbinding.ExtractOperationResult
import net.sf.sevenzipjbinding.IArchiveExtractCallback
import net.sf.sevenzipjbinding.ISequentialOutStream
import net.sf.sevenzipjbinding.PropID
import net.sf.sevenzipjbinding.SevenZip
import net.sf.sevenzipjbinding.impl.RandomAccessFileInStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.io.OutputStream
import java.io.RandomAccessFile
import java.nio.charset.Charset

/**
 * Extracts the archives that mods and translations come in: ZIP (the format is read from the first bytes, not the name) and RAR (3/4/5) or 7z through 7-Zip-JBinding
 */
object ArchiveUtils {
    val archiveExtensions = setOf("zip", "rar", "7z")

    private fun startsWith(header : ByteArray, vararg bytes : Int) = header.size >= bytes.size && bytes.indices.all { (header[it].toInt() and 0xFF) == bytes[it] }

    /** @return "zip", "rar", "7z" or null when the file isn't an archive that can be read */
    fun detectFormat(file : File) : String? {
        val header = ByteArray(8)
        val read = file.inputStream().use { it.read(header) }
        if (read < 4)
            return null
        return when {
            startsWith(header, 0x50, 0x4B, 0x03, 0x04) || startsWith(header, 0x50, 0x4B, 0x05, 0x06) -> "zip"
            startsWith(header, 0x52, 0x61, 0x72, 0x21, 0x1A, 0x07) -> "rar"
            startsWith(header, 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C) -> "7z"
            else -> null
        }
    }

    /** @return The destination of an entry inside [target], null if the name would leave it (Zip Slip) */
    private fun safeDestination(target : File, entryName : String) : File? {
        val name = entryName.replace('\\', '/').trimStart('/')
        if (name.isEmpty())
            return null
        val destination = File(target, name)
        return if (destination.canonicalPath.startsWith(target.canonicalPath + File.separator)) destination else null
    }

    private fun extractZip(file : File, target : File) {
        try {
            ZipUtils.unzip(file, target)
        } catch (e : IllegalArgumentException) {
            // File names that aren't UTF-8 (zips made by old Windows tools) are read again with a single byte charset
            target.deleteRecursively()
            target.mkdirs()
            extractZipWithCharset(file, target, Charsets.ISO_8859_1)
        }
    }

    private fun extractZipWithCharset(file : File, target : File, charset : Charset) {
        java.util.zip.ZipFile(file, charset).use { zip ->
            for (entry in zip.entries()) {
                val destination = safeDestination(target, entry.name) ?: continue
                if (entry.isDirectory) {
                    destination.mkdirs()
                    continue
                }
                destination.parentFile?.mkdirs()
                zip.getInputStream(entry).use { input -> destination.outputStream().use { input.copyTo(it) } }
            }
        }
    }

    private fun extractWithSevenZip(file : File, target : File) {
        RandomAccessFile(file, "r").use { random ->
            // The format is detected from the content
            val archive = SevenZip.openInArchive(null, RandomAccessFileInStream(random))
            try {
                var failed = 0
                var output : OutputStream? = null
                fun closeOutput() {
                    output?.close()
                    output = null
                }

                // The whole archive is extracted in one pass, extracting item by item would decode a solid archive again for every file
                archive.extract(null, false, object : IArchiveExtractCallback {
                    override fun getStream(index : Int, extractAskMode : ExtractAskMode) : ISequentialOutStream? {
                        closeOutput()
                        if (extractAskMode != ExtractAskMode.EXTRACT)
                            return null
                        if (archive.getProperty(index, PropID.IS_FOLDER) as? Boolean == true) {
                            archive.getStringProperty(index, PropID.PATH)?.let { safeDestination(target, it)?.mkdirs() }
                            return null
                        }
                        val path = archive.getStringProperty(index, PropID.PATH) ?: return null
                        val destination = safeDestination(target, path) ?: return null
                        destination.parentFile?.mkdirs()
                        val stream = FileOutputStream(destination)
                        output = stream
                        return ISequentialOutStream { data ->
                            stream.write(data)
                            data.size
                        }
                    }

                    override fun prepareOperation(extractAskMode : ExtractAskMode) {}

                    override fun setOperationResult(extractOperationResult : ExtractOperationResult) {
                        closeOutput()
                        if (extractOperationResult != ExtractOperationResult.OK)
                            failed++
                    }

                    override fun setTotal(total : Long) {}

                    override fun setCompleted(complete : Long) {}
                })
                closeOutput()
                if (failed > 0)
                    throw IOException("$failed file(s) of the archive couldn't be extracted (password, damaged or missing part)")
            } finally {
                archive.close()
            }
        }
    }

    /**
     * Extracts [file] (ZIP, RAR or 7z) into [target]
     * @exception IOException if the archive can't be read
     */
    @Throws(IOException::class)
    fun extract(file : File, target : File) {
        target.mkdirs()
        val format = detectFormat(file) ?: when (file.extension.lowercase()) {
            "zip" -> "zip"
            "rar" -> "rar"
            "7z" -> "7z"
            else -> null
        } ?: throw IOException("Unknown archive format")

        if (format == "zip") {
            extractZip(file, target)
        } else {
            try {
                extractWithSevenZip(file, target)
            } catch (e : UnsatisfiedLinkError) {
                throw IOException("RAR/7z support couldn't be loaded on this device: ${e.message}")
            } catch (e : NoClassDefFoundError) {
                throw IOException("RAR/7z support couldn't be loaded on this device: ${e.message}")
            } catch (e : net.sf.sevenzipjbinding.SevenZipException) {
                throw IOException("The $format file can't be read: ${e.message}", e)
            }
        }
    }
}
