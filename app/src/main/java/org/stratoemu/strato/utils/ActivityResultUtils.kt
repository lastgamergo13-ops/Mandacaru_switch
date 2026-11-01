/*
 * SPDX-License-Identifier: MPL-2.0
 */

package org.stratoemu.strato.utils

import androidx.activity.ComponentActivity
import androidx.activity.result.ActivityResultCallback
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContract
import java.util.concurrent.atomic.AtomicInteger

private val launcherCounter = AtomicInteger()

/**
 * Registers an activity result launcher that can be created at any moment
 * [ComponentActivity.registerForActivityResult] throws IllegalStateException once the activity is started, and preferences, dialogs and settings tabs are created long after that (every tab of the settings is a new fragment with new preferences), so they register directly in the registry with a key of their own
 */
fun <I, O> ComponentActivity.registerLauncher(contract : ActivityResultContract<I, O>, callback : ActivityResultCallback<O>) : ActivityResultLauncher<I> =
    activityResultRegistry.register("launcher_${launcherCounter.getAndIncrement()}", contract, callback)
