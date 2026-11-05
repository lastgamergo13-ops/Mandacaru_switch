/*
 * SPDX-License-Identifier: MPL-2.0
 */

package org.stratoemu.strato.settings

import android.annotation.SuppressLint
import android.content.SharedPreferences
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.isVisible
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.preference.Preference
import androidx.preference.PreferenceManager
import com.google.android.material.button.MaterialButton
import org.stratoemu.strato.R
import org.stratoemu.strato.databinding.SetupFragmentBinding
import org.stratoemu.strato.di.getSettings
import org.stratoemu.strato.preference.FirmwareImportPreference
import org.stratoemu.strato.preference.FolderPickerPreference
import org.stratoemu.strato.preference.KeyPickerPreference
import java.io.File

/**
 * The first-run setup: one page at a time (welcome, keys, firmware, games folder and done) with the buttons of the page in the middle and Back/Next at the bottom
 * The file pickers are the same preferences the settings use, they are only kept out of sight in a screen of their own and opened from the buttons of the pages
 */
class SetupFragment : Fragment() {
    private class StepButton(val titleRes : Int, val iconRes : Int, val isDone : () -> Boolean, val action : () -> Unit)

    private class Step(val iconRes : Int, val titleRes : Int, val descriptionRes : Int, val buttons : List<StepButton>)

    private var binding : SetupFragmentBinding? = null
    private var currentStep = 0

    private lateinit var prodKeys : KeyPickerPreference
    private lateinit var titleKeys : KeyPickerPreference
    private lateinit var firmware : FirmwareImportPreference
    private lateinit var gamesFolder : FolderPickerPreference
    private lateinit var steps : List<Step>

    /** The firmware is imported in the background and saves its version when it's done, the page is refreshed when that happens */
    private val preferencesListener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> view?.post { showStep() } }

    private val keysDirectory get() = File(requireContext().filesDir, "keys")

    private fun keysImported(name : String) = File(keysDirectory, name).exists()

    private fun firmwareInstalled() = !PreferenceManager.getDefaultSharedPreferences(requireContext()).getString("firmware", null).isNullOrBlank()

    @SuppressLint("RestrictedApi")
    override fun onCreate(savedInstanceState : Bundle?) {
        super.onCreate(savedInstanceState)

        // The pickers register their launchers when they are created, that has to happen before the screen is started
        val activity = requireActivity()
        val screen = PreferenceManager(activity).createPreferenceScreen(activity)
        fun <T : Preference> keep(preference : T, preferenceKey : String) : T {
            preference.key = preferenceKey
            screen.addPreference(preference)
            return preference
        }

        prodKeys = keep(KeyPickerPreference(activity), "prod_keys")
        titleKeys = keep(KeyPickerPreference(activity), "title_keys")
        firmware = keep(FirmwareImportPreference(activity), "firmware")
        gamesFolder = keep(FolderPickerPreference(activity), "search_location")

        currentStep = savedInstanceState?.getInt("step") ?: 0

        steps = listOf(
            Step(R.drawable.ic_arrow_forward, R.string.setup_welcome, R.string.setup_welcome_message, emptyList()),
            Step(R.drawable.ic_vpn_key, R.string.setup_keys, R.string.setup_keys_description, listOf(
                StepButton(R.string.setup_button_prod_keys, R.drawable.ic_vpn_key, { keysImported("prod.keys") }) { prodKeys.pick() },
                StepButton(R.string.setup_button_title_keys, R.drawable.ic_vpn_key, { keysImported("title.keys") }) { titleKeys.pick() }
            )),
            Step(R.drawable.ic_memory, R.string.setup_firmware, R.string.setup_firmware_description, listOf(
                StepButton(R.string.setup_button_firmware, R.drawable.ic_memory, { firmwareInstalled() }) {
                    // The row is disabled when it was created without keys, they may have been imported since then
                    firmware.isEnabled = keysImported("prod.keys")
                    if (firmware.isEnabled)
                        firmware.pick()
                    else
                        com.google.android.material.snackbar.Snackbar.make(requireView(), R.string.firmware_keys_needed, com.google.android.material.snackbar.Snackbar.LENGTH_LONG).show()
                }
            )),
            Step(R.drawable.ic_folder, R.string.setup_games, R.string.setup_games_description, listOf(
                StepButton(R.string.setup_button_games, R.drawable.ic_folder, { requireContext().getSettings().searchLocations.isNotEmpty() }) { gamesFolder.pick() }
            )),
            Step(R.drawable.ic_check_circle, R.string.setup_finished, R.string.setup_finished_description, listOf(
                StepButton(R.string.setup_get_started, R.drawable.ic_arrow_forward, { false }) { requireActivity().finish() }
            ))
        )

        PreferenceManager.getDefaultSharedPreferences(activity).registerOnSharedPreferenceChangeListener(preferencesListener)
    }

    override fun onCreateView(inflater : LayoutInflater, container : ViewGroup?, savedInstanceState : Bundle?) : View {
        return SetupFragmentBinding.inflate(inflater, container, false).also { binding = it }.root
    }

    override fun onViewCreated(view : View, savedInstanceState : Bundle?) {
        val binding = binding!!

        // Keeps the buttons clear of the system bars and the camera cutout when they are shown
        ViewCompat.setOnApplyWindowInsetsListener(binding.root) { v, windowInsets ->
            val insets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            v.updatePadding(insets.left, insets.top, insets.right, insets.bottom)
            WindowInsetsCompat.CONSUMED
        }
        binding.buttonNext.setOnClickListener {
            if (currentStep < steps.size - 1) {
                currentStep++
                showStep()
            }
        }
        binding.buttonBack.setOnClickListener {
            if (currentStep > 0) {
                currentStep--
                showStep()
            }
        }
        showStep()
    }

    override fun onResume() {
        super.onResume()
        // Coming back from a file picker
        showStep()
    }

    override fun onSaveInstanceState(outState : Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt("step", currentStep)
    }

    override fun onDestroy() {
        PreferenceManager.getDefaultSharedPreferences(requireContext()).unregisterOnSharedPreferenceChangeListener(preferencesListener)
        super.onDestroy()
    }

    override fun onDestroyView() {
        binding = null
        super.onDestroyView()
    }

    private fun showStep() {
        val binding = binding ?: return
        val step = steps[currentStep]

        binding.icon.setImageResource(step.iconRes)
        binding.textTitle.setText(step.titleRes)
        binding.textDescription.setText(step.descriptionRes)

        binding.buttonContainer.removeAllViews()
        step.buttons.forEach { stepButton ->
            val button = layoutInflater.inflate(R.layout.setup_page_button, binding.buttonContainer, false) as MaterialButton
            val done = stepButton.isDone()
            button.setText(stepButton.titleRes)
            button.setIconResource(if (done) R.drawable.ic_check_circle else stepButton.iconRes)
            button.setOnClickListener { stepButton.action() }
            binding.buttonContainer.addView(button)
        }

        // The page is complete when all its buttons were used (the last page has nothing left to do)
        val isFinalStep = currentStep == steps.size - 1
        val complete = step.buttons.isNotEmpty() && !isFinalStep && step.buttons.all { it.isDone() || it.titleRes == R.string.setup_button_title_keys }
        binding.textConfirmation.isVisible = complete

        binding.buttonBack.visibility = if (currentStep == 0) View.INVISIBLE else View.VISIBLE
        binding.buttonNext.isVisible = !isFinalStep
        binding.buttonNext.setText(if (step.buttons.isEmpty() || complete) R.string.setup_next else R.string.setup_skip_step)
    }
}
