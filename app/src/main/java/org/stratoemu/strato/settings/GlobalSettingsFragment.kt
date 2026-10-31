/*
 * SPDX-License-Identifier: MPL-2.0
 * Copyright © 2023 Skyline Team and Contributors (https://github.com/skyline-emu/)
 */

package org.stratoemu.strato.settings

import android.content.Intent
import android.os.Bundle
import android.view.View
import androidx.preference.Preference
import androidx.preference.PreferenceCategory
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.SwitchPreferenceCompat
import androidx.preference.TwoStatePreference
import androidx.window.layout.FoldingFeature
import androidx.window.layout.WindowInfoTracker
import org.stratoemu.strato.BuildConfig
import org.stratoemu.strato.MainActivity
import org.stratoemu.strato.R
import org.stratoemu.strato.utils.GpuDriverHelper
import org.stratoemu.strato.utils.WindowInsetsHelper
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * This fragment is used to display the global preferences
 */
class GlobalSettingsFragment : PreferenceFragmentCompat() {
    companion object {
        /** The argument with the key of the category (tab) this fragment shows, without it the list of tabs is shown */
        const val SectionTag = "settings_section"

        /** The key of the category that holds the list of tabs */
        const val MenuCategoryKey = "category_menu"

        /** The tabs of the settings in the order they are listed: the key of the category and its title */
        val sections = listOf(
            "category_content" to R.string.content,
            "category_appearance" to R.string.appearance,
            "category_system" to R.string.system,
            "category_presentation" to R.string.display,
            "category_audio" to R.string.audio,
            "category_gpu" to R.string.gpu,
            "category_input" to R.string.controls,
            "category_hacks" to R.string.hackers,
            "category_debug" to R.string.debug,
            "category_licenses" to R.string.licenses
        )
    }

    private val section get() = arguments?.getString(SectionTag)

    override fun onResume() {
        super.onResume()
        // Every tab is a screen of its own with its name in the toolbar
        val title = sections.firstOrNull { it.first == section }?.second ?: R.string.settings
        (activity as? androidx.appcompat.app.AppCompatActivity)?.supportActionBar?.title = getString(title)
    }

    /**
     * Shows either the list of tabs or the category of the tab this fragment is for, every category is always in the tree so the code that looks preferences up keeps working
     */
    fun applySectionVisibility() {
        val screen = preferenceScreen ?: return
        for (index in 0 until screen.preferenceCount) {
            val category = screen.getPreference(index)
            category.isVisible = if (category.key == MenuCategoryKey) section == null else (section != null && category.key == section)
        }
    }

    private fun addTabMenu() {
        val menu = PreferenceCategory(requireContext()).apply {
            key = MenuCategoryKey
            isIconSpaceReserved = false
        }
        preferenceScreen.addPreference(menu)

        sections.forEach { (categoryKey, titleRes) ->
            // The debug tab is only for debug builds
            @Suppress("SENSELESS_COMPARISON")
            if (categoryKey == "category_debug" && BuildConfig.BUILD_TYPE == "release")
                return@forEach

            menu.addPreference(Preference(requireContext()).apply {
                key = "tab_$categoryKey"
                title = getString(titleRes)
                isIconSpaceReserved = false
                setOnPreferenceClickListener {
                    (activity as? SettingsActivity)?.openSection(categoryKey)
                    true
                }
            })
        }
    }

    override fun onViewCreated(view : View, savedInstanceState : Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val recyclerView = view.findViewById<View>(androidx.preference.R.id.recycler_view)
        WindowInsetsHelper.setPadding(recyclerView, bottom = true)
    }

    /**
     * This constructs the preferences from XML preference resources
     */
    override fun onCreatePreferences(savedInstanceState : Bundle?, rootKey : String?) {
        addPreferencesFromResource(R.xml.app_preferences)
        addPreferencesFromResource(R.xml.emulation_preferences)
        addPreferencesFromResource(R.xml.input_preferences)
        addPreferencesFromResource(R.xml.credits_preferences)

        // Re-launch the app if Material You is toggled
        findPreference<Preference>("use_material_you")?.setOnPreferenceChangeListener { _, _ ->
            requireActivity().finishAffinity()
            startActivity(Intent(requireContext(), MainActivity::class.java))
            true
        }

        CoroutineScope(Dispatchers.IO).launch {
            WindowInfoTracker.getOrCreate(requireContext()).windowLayoutInfo(requireActivity()).collect { newLayoutInfo ->
                withContext(Dispatchers.Main) {
                    findPreference<SwitchPreferenceCompat>("enable_foldable_layout")?.isVisible = newLayoutInfo.displayFeatures.find { it is FoldingFeature } != null
                }
            }
        }

        // Uncheck `disable_frame_throttling` if `force_triple_buffering` gets disabled
        val disableFrameThrottlingPref = findPreference<TwoStatePreference>("disable_frame_throttling")!!
        findPreference<TwoStatePreference>("force_triple_buffering")?.setOnPreferenceChangeListener { _, newValue ->
            if (newValue == false)
                disableFrameThrottlingPref.isChecked = false
            true
        }

        // Only show validation layer setting in debug builds
        @Suppress("SENSELESS_COMPARISON")
        if (BuildConfig.BUILD_TYPE != "release")
            findPreference<Preference>("validation_layer")?.isVisible = true

        if (!GpuDriverHelper.supportsForceMaxGpuClocks()) {
            val forceMaxGpuClocksPref = findPreference<TwoStatePreference>("force_max_gpu_clocks")!!
            forceMaxGpuClocksPref.isSelectable = false
            forceMaxGpuClocksPref.isChecked = false
            forceMaxGpuClocksPref.summary = context!!.getString(R.string.force_max_gpu_clocks_desc_unsupported)
        }

        addTabMenu()
        applySectionVisibility()
    }
}
