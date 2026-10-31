/*
 * SPDX-License-Identifier: MPL-2.0
 */

package org.stratoemu.strato.preference

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import androidx.appcompat.app.AppCompatDelegate
import androidx.preference.R
import org.stratoemu.strato.settings.ThemeOptions

/**
 * The only theme setting, it chooses the color and the light/dark mode together (see [ThemeOptions]), the screen is recreated after the value was saved so every color changes right away
 */
class ThemeOptionPreference @JvmOverloads constructor(context : Context, attrs : AttributeSet? = null, defStyleAttr : Int = R.attr.dialogPreferenceStyle) : IntegerListPreference(context, attrs, defStyleAttr) {
    override fun callChangeListener(newValue : Any?) : Boolean {
        val accepted = super.callChangeListener(newValue)

        if (accepted) {
            AppCompatDelegate.setDefaultNightMode(ThemeOptions.nightMode(newValue as Int))

            // The value is saved after this function returns, so the activity is recreated afterwards
            val activity = context as? Activity
            Handler(Looper.getMainLooper()).post { activity?.recreate() }
        }

        return accepted
    }
}
