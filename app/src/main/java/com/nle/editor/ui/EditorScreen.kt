package com.nle.editor.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.weight
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nle.editor.ui.media.MediaBrowserPanel
import com.nle.editor.ui.preview.PreviewPanel
import com.nle.editor.ui.properties.PropertiesPanel
import com.nle.editor.ui.timeline.SelectedClip
import com.nle.editor.ui.timeline.TimelinePanel
import com.nle.editor.viewmodel.EditorViewModel

private enum class BottomTab { MEDIA, PROPERTIES }

/**
 * Composition root for the editor. This function -- and every function it
 * calls -- only ever reads [EditorViewModel.uiState] and calls the
 * ViewModel's `on...` methods; nothing under this tree holds a
 * [com.nle.editor.engine.NativeEditorEngine] reference or calls JNI
 * directly. That's the whole point of the layering described across
 * EditorViewModel.kt / EditorRepository.kt / NativeEditorEngine.kt: this
 * file can be redesigned freely (different panel arrangement, tablet vs
 * phone layout, etc.) without touching a single native-facing line.
 */
@Composable
fun EditorScreen(viewModel: EditorViewModel = viewModel()) {
    val state by viewModel.uiState.collectAsState()
    var selectedClip by remember { mutableStateOf<SelectedClip?>(null) }
    var bottomTab by remember { mutableStateOf(BottomTab.MEDIA) }

    Column(modifier = Modifier.fillMaxSize()) {
        PreviewPanel(state = state, viewModel = viewModel, modifier = Modifier.weight(0.45f))

        TimelinePanel(
            state = state,
            viewModel = viewModel,
            selectedClip = selectedClip,
            onClipSelected = { selectedClip = it },
            modifier = Modifier.weight(0.35f),
        )

        TabRow(selectedTabIndex = bottomTab.ordinal) {
            Tab(selected = bottomTab == BottomTab.MEDIA, onClick = { bottomTab = BottomTab.MEDIA },
                text = { androidx.compose.material3.Text("Media") })
            Tab(selected = bottomTab == BottomTab.PROPERTIES, onClick = { bottomTab = BottomTab.PROPERTIES },
                text = { androidx.compose.material3.Text("Properties") })
        }

        when (bottomTab) {
            BottomTab.MEDIA -> MediaBrowserPanel(viewModel = viewModel, modifier = Modifier.weight(0.2f))
            BottomTab.PROPERTIES -> PropertiesPanel(
                state = state, viewModel = viewModel, selectedClip = selectedClip, modifier = Modifier.weight(0.2f),
            )
        }
    }
}
