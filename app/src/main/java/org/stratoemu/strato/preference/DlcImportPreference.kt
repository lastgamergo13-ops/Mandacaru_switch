/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.preference

import android.content.Context
import android.util.AttributeSet
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import androidx.preference.Preference
import androidx.preference.Preference.SummaryProvider
import com.google.android.material.snackbar.Snackbar
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import org.stratoemu.strato.R
import org.stratoemu.strato.fragments.IndeterminateProgressDialogFragment
import org.stratoemu.strato.getPublicFilesDir
import org.stratoemu.strato.settings.SettingsActivity
import java.io.File

/**
 * This preference installs the content of NSP files (DLC and updates) so the games can use it, the files are copied to the emulator's own content directory
 */
class DlcImportPreference @JvmOverloads constructor(context : Context, attrs : AttributeSet? = null, defStyleAttr : Int = androidx.preference.R.attr.preferenceStyle) : Preference(context, attrs, defStyleAttr) {
    private val contentsPath = File(context.getPublicFilesDir().canonicalPath + "/switch/nand/user/Contents/registered/")
    private val titleKeysPath = File(context.getPublicFilesDir().canonicalPath + "/switch/nand/user/Contents/title_keys.txt")

    private val documentPicker = (context as ComponentActivity).registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isNotEmpty()) {
            val task : () -> Unit = {
                var installed = 0
                var failed = 0

                uris.forEach { uri ->
                    try {
                        val descriptor = context.contentResolver.openFileDescriptor(uri, "r")
                        if (descriptor == null) {
                            failed++
                        } else {
                            // The native code takes ownership of the descriptor and closes it, this avoids copying a file that can be several GB to the cache first
                            val count = installNsp(descriptor.detachFd(), contentsPath.path, titleKeysPath.path)
                            if (count >= 0)
                                installed += count
                            else
                                failed++
                        }
                    } catch (e : Exception) {
                        failed++
                    }
                }

                val message = context.getString(R.string.install_content_result, installed, failed)
                CoroutineScope(Dispatchers.Main).launch {
                    Snackbar.make((context as SettingsActivity).binding.root, message, Snackbar.LENGTH_LONG).show()
                    notifyChanged()
                }
            }

            IndeterminateProgressDialogFragment.newInstance(context as SettingsActivity, R.string.install_content_in_progress, task)
                .show(context.supportFragmentManager, IndeterminateProgressDialogFragment.TAG)
        }
    }

    init {
        summaryProvider = SummaryProvider<Preference> {
            val count = contentsPath.listFiles()?.count { it.isFile && it.extension.equals("nca", ignoreCase = true) } ?: 0
            context.getString(R.string.content_installed, count)
        }
    }

    override fun onClick() = documentPicker.launch(arrayOf("*/*"))

    /**
     * Copies the NCAs of the NSP to [contentsPath] and the title keys of its tickets to [titleKeysPath]
     * @param fd The file descriptor of the NSP, it is closed by this function
     * @return The number of NCAs that were copied or -1 if the NSP couldn't be read
     */
    private external fun installNsp(fd : Int, contentsPath : String, titleKeysPath : String) : Int
}
