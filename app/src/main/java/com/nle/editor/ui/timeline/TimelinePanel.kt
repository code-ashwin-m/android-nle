package com.nle.editor.ui.timeline

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Redo
import androidx.compose.material.icons.filled.Undo
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import com.nle.editor.engine.UiClip
import com.nle.editor.engine.UiTrack
import com.nle.editor.viewmodel.EditorUiState
import com.nle.editor.viewmodel.EditorViewModel

/**
 * Everything here reads [EditorUiState.project] (the mirrored native
 * timeline) and turns gestures into [EditorViewModel] calls -- exactly the
 * "UI must never directly manipulate video" boundary the spec draws.
 * Clip rectangles are pure derived layout (pixelsPerUs * clip fields), so
 * there's no client-side timeline state that could drift from what native
 * holds; every drag/trim/split ends by calling into the ViewModel and
 * waiting for the next snapshot to redraw, rather than optimistically
 * moving a clip locally first.
 */
@Composable
fun TimelinePanel(state: EditorUiState, viewModel: EditorViewModel, selectedClip: SelectedClip?,
                   onClipSelected: (SelectedClip?) -> Unit, modifier: Modifier = Modifier) {
    var pixelsPerUs by remember { mutableStateOf(0.00006f) }  // zoom level; ~1px per ~16ms at default
    var snapEnabled by remember { mutableStateOf(true) }

    Column(modifier = modifier) {
        TimelineToolbar(
            viewModel = viewModel,
            selectedClip = selectedClip,
            snapEnabled = snapEnabled,
            onSnapToggle = { snapEnabled = !snapEnabled },
            onZoomChange = { factor -> pixelsPerUs *= factor },
        )
        TimelineRuler(state.project?.durationUs ?: 0L, pixelsPerUs)

        val scrollState = rememberScrollState()
        Box(modifier = Modifier.fillMaxWidth().horizontalScroll(scrollState)) {
            Column {
                state.project?.tracks?.forEach { track ->
                    TrackRow(
                        track = track,
                        pixelsPerUs = pixelsPerUs,
                        selectedClip = selectedClip,
                        onClipTap = { clip -> onClipSelected(SelectedClip(track.id, clip.id)) },
                    )
                }
            }
            Playhead(state.currentTimeUs, pixelsPerUs)
        }
    }
}

data class SelectedClip(val trackId: Long, val clipId: Long)

@Composable
private fun TimelineToolbar(
    viewModel: EditorViewModel,
    selectedClip: SelectedClip?,
    snapEnabled: Boolean,
    onSnapToggle: () -> Unit,
    onZoomChange: (Float) -> Unit,
) {
    Row(modifier = Modifier.fillMaxWidth().padding(4.dp)) {
        IconButton(onClick = { viewModel.onUndo() }) { Icon(Icons.Filled.Undo, contentDescription = "Undo") }
        IconButton(onClick = { viewModel.onRedo() }) { Icon(Icons.Filled.Redo, contentDescription = "Redo") }
        IconButton(
            enabled = selectedClip != null,
            onClick = { selectedClip?.let { viewModel.onSplitClipAtPlayhead(it.trackId, it.clipId) } },
        ) { Icon(Icons.Filled.ContentCut, contentDescription = "Split") }
        IconButton(
            enabled = selectedClip != null,
            onClick = { selectedClip?.let { viewModel.onDeleteClip(it.trackId, it.clipId, ripple = false) } },
        ) { Icon(Icons.Filled.Delete, contentDescription = "Delete") }
        Text(if (snapEnabled) "Snap: On" else "Snap: Off", modifier = Modifier.padding(8.dp))
        IconButton(onClick = onSnapToggle) { Text("Snap") }
        IconButton(onClick = { onZoomChange(1.25f) }) { Text("+") }
        IconButton(onClick = { onZoomChange(0.8f) }) { Text("-") }
    }
}

@Composable
private fun TimelineRuler(durationUs: Long, pixelsPerUs: Float) {
    // Minimal Phase 1 ruler: tick marks every second. A production ruler
    // adapts tick spacing to zoom level (frames when zoomed in, minutes
    // when zoomed out) -- left as a follow-up once real footage lengths
    // are being tested against, rather than guessed at now.
    val secondWidthDp = (1_000_000f * pixelsPerUs).dp
    Row(modifier = Modifier.fillMaxWidth().height(24.dp).background(Color.DarkGray)) {
        val totalSeconds = (durationUs / 1_000_000L).toInt().coerceAtLeast(1)
        repeat(totalSeconds) { second ->
            Text(text = "${second}s", modifier = Modifier.width(secondWidthDp), color = Color.White)
        }
    }
}

@Composable
private fun TrackRow(track: UiTrack, pixelsPerUs: Float, selectedClip: SelectedClip?, onClipTap: (UiClip) -> Unit) {
    Box(modifier = Modifier.fillMaxWidth().height(64.dp).background(trackBackgroundColor(track))) {
        track.clips.forEach { clip ->
            val startDp = (clip.timelineStartUs * pixelsPerUs).dp
            val widthDp = (clip.durationUs * pixelsPerUs).dp
            val isSelected = selectedClip?.clipId == clip.id
            Box(
                modifier = Modifier
                    .padding(start = startDp)
                    .width(widthDp)
                    .height(64.dp)
                    .background(if (isSelected) Color(0xFF3D7BFF) else Color(0xFF5C5C5C))
                    .pointerInput(clip.id) { detectTapGestures(onTap = { onClipTap(clip) }) },
            ) {
                Text(text = "Clip ${clip.id}", color = Color.White, modifier = Modifier.padding(4.dp))
            }
        }
    }
}

@Composable
private fun Playhead(currentTimeUs: Long, pixelsPerUs: Float) {
    val xDp = (currentTimeUs * pixelsPerUs).dp
    Box(
        modifier = Modifier
            .padding(start = xDp)
            .width(2.dp)
            .fillMaxHeight()
            .background(Color.Red),
    )
}

private fun trackBackgroundColor(track: UiTrack) = Color(0xFF2B2B2B)
