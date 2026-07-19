package com.nle.editor.ui.properties

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.nle.editor.engine.EffectType
import com.nle.editor.ui.timeline.SelectedClip
import com.nle.editor.viewmodel.EditorUiState
import com.nle.editor.viewmodel.EditorViewModel

/**
 * Phase 1 has exactly one control: Brightness. This file is written as the
 * template every future property (Contrast, Exposure, Saturation,
 * Temperature, Tint, Opacity, Scale, Rotation, Crop, Mask, Blur) will
 * copy: find the selected clip's effect of the matching type, render one
 * [Slider] bound to it, call the matching ViewModel setter on every
 * `onValueChange`. Adding Contrast later means one more `when` branch
 * below and one more ViewModel method with the same shape as
 * [EditorViewModel.onBrightnessChanged] -- not a new panel architecture.
 */
@Composable
fun PropertiesPanel(state: EditorUiState, viewModel: EditorViewModel, selectedClip: SelectedClip?, modifier: Modifier = Modifier) {
    Column(modifier = modifier.padding(12.dp)) {
        if (selectedClip == null) {
            Text("Select a clip to edit its properties.")
            return@Column
        }

        val clip = state.project?.tracks?.firstOrNull { it.id == selectedClip.trackId }
            ?.clips?.firstOrNull { it.id == selectedClip.clipId }

        if (clip == null) {
            Text("Selected clip no longer exists.")
            return@Column
        }

        val brightnessEffect = clip.effects.firstOrNull { it.type == EffectType.BRIGHTNESS }

        if (brightnessEffect == null) {
            Button(onClick = { viewModel.onAddBrightnessEffect(selectedClip.trackId, selectedClip.clipId) }) {
                Text("Add Brightness")
            }
        } else {
            val value = brightnessEffect.properties["brightness"] ?: 0.0
            Text("Brightness")
            Row {
                Slider(
                    value = value.toFloat(),
                    valueRange = -1f..1f,
                    onValueChange = { newValue ->
                        viewModel.onBrightnessChanged(
                            selectedClip.trackId, selectedClip.clipId, brightnessEffect.id, newValue.toDouble(),
                        )
                    },
                )
                Text(text = "%.2f".format(value))
            }
        }
    }
}
