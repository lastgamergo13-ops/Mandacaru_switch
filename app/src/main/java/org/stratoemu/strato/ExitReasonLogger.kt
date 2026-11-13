/*
 * SPDX-License-Identifier: MPL-2.0
 */

package org.stratoemu.strato

import android.app.ActivityManager
import android.app.ApplicationExitInfo
import android.content.Context
import android.os.Build
import android.util.Log
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Writes why the previous runs of this app's process ended to `last_exit_reasons.txt` in the public files directory (and to logcat).
 * A game that is killed by the system (the low memory killer, a native crash) doesn't get to write anything to the emulator log, this is the only place where the cause
 * (e.g. REASON_LOW_MEMORY, REASON_CRASH_NATIVE with the signal, REASON_SIGNALED) can be read. Needs Android 11+, older versions don't have the API.
 */
object ExitReasonLogger {
    private const val TAG = "ExitReasonLogger"
    private const val MaxEntries = 6

    fun record(context : Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R)
            return

        try {
            val activityManager = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager ?: return
            val reasons = activityManager.getHistoricalProcessExitReasons(null, 0, MaxEntries)
            if (reasons.isEmpty())
                return

            val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US)
            val text = reasons.joinToString("\n") { info ->
                "${dateFormat.format(Date(info.timestamp))} | process '${info.processName}' | reason: ${reasonName(info.reason)} | status: ${info.status} | importance: ${info.importance} | " +
                    "pss: ${info.pss} kB | rss: ${info.rss} kB | description: ${info.description ?: "-"}"
            }

            Log.i(TAG, "Previous process exits:\n$text")
            context.getPublicFilesDir().resolve("last_exit_reasons.txt").writeText(text + "\n")
        } catch (e : Exception) {
            Log.w(TAG, "Couldn't read the process exit reasons", e)
        }
    }

    private fun reasonName(reason : Int) : String = when (reason) {
        ApplicationExitInfo.REASON_ANR -> "ANR"
        ApplicationExitInfo.REASON_CRASH -> "CRASH (Java)"
        ApplicationExitInfo.REASON_CRASH_NATIVE -> "CRASH_NATIVE"
        ApplicationExitInfo.REASON_DEPENDENCY_DIED -> "DEPENDENCY_DIED"
        ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE -> "EXCESSIVE_RESOURCE_USAGE"
        ApplicationExitInfo.REASON_EXIT_SELF -> "EXIT_SELF"
        ApplicationExitInfo.REASON_INITIALIZATION_FAILURE -> "INITIALIZATION_FAILURE"
        ApplicationExitInfo.REASON_LOW_MEMORY -> "LOW_MEMORY (killed by the system for lack of RAM)"
        ApplicationExitInfo.REASON_OTHER -> "OTHER"
        ApplicationExitInfo.REASON_PERMISSION_CHANGE -> "PERMISSION_CHANGE"
        ApplicationExitInfo.REASON_SIGNALED -> "SIGNALED"
        ApplicationExitInfo.REASON_UNKNOWN -> "UNKNOWN"
        ApplicationExitInfo.REASON_USER_REQUESTED -> "USER_REQUESTED"
        ApplicationExitInfo.REASON_USER_STOPPED -> "USER_STOPPED"
        else -> "reason $reason"
    }
}
