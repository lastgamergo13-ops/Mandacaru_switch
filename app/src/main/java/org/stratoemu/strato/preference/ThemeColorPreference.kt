/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.preference

import android.app.Activity
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.AttributeSet
import androidx.preference.R

/**
 * This preference is used to choose the color of the theme, the screen is recreated after the new value was saved so the new colors are used right away
 */
class ThemeColorPreference @JvmOverloads constructor(context : Context, attrs : AttributeSet? = null, defStyleAttr : Int = R.attr.dialogPreferenceStyle) : IntegerListPreference(context, attrs, defStyleAttr) {
    override fun callChangeListener(newValue : Any?) : Boolean {
        val accepted = super.callChangeListener(newValue)

        if (accepted) {
            // The value is saved after this function returns, so the activity is recreated afterwards
            val activity = context as? Activity
            Handler(Looper.getMainLooper()).post { activity?.recreate() }
        }

        return accepted
    }
}
