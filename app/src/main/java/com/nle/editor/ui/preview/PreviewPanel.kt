package com.nle.editor.ui.preview

import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SkipNext
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import com.nle.editor.engine.PlaybackState
import com.nle.editor.engine.UiRenderStats
import com.nle.editor.viewmodel.EditorUiState
import com.nle.editor.viewmodel.EditorViewModel
import kotlin.math.roundToLong

/**
 * The only Compose surface that talks to a raw Android View
 * ([SurfaceView]) instead of drawing declaratively -- necessary because
 * the render graph's [PreviewOutputNode][../../../../cpp/rendergraph/nodes/OutputNode.h]
 * needs a real [android.view.Surface] to create an EGL window surface
 * against. Everything else in this file (transport controls, timecode,
 * stats overlay) is ordinary Compose reading from [EditorViewModel.uiState].
 */
@Composable
fun PreviewPanel(state: EditorUiState, viewModel: EditorViewModel, modifier: Modifier = Modifier) {
    Column(modifier = modifier) {
        Box(modifier = Modifier.fillMaxWidth().aspectRatio(9f / 16f).background(Color.Black)) {
            PreviewSurface(viewModel)
            RenderStatsOverlay(state.renderStats, modifier = Modifier.align(Alignment.TopEnd).padding(8.dp))
        }
        TransportControls(state, viewModel)
    }
}

@Composable
private fun PreviewSurface(viewModel: EditorViewModel) {
    AndroidView(
        modifier = Modifier.fillMaxWidth().aspectRatio(9f / 16f),
        factory = { context ->
            SurfaceView(context).apply {
                holder.addCallback(object : SurfaceHolder.Callback {
                    override fun surfaceCreated(holder: SurfaceHolder) {
                        viewModel.onPreviewSurfaceReady(holder.surface)
                    }
                    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}
                    override fun surfaceDestroyed(holder: SurfaceHolder) {
                        viewModel.onPreviewSurfaceDestroyed()
                    }
                })
            }
        },
    )
}

@Composable
private fun RenderStatsOverlay(stats: UiRenderStats, modifier: Modifier = Modifier) {
    // "Render statistics: FPS, Decoder FPS, Renderer FPS, Dropped frames,
    // GPU time" -- a plain text overlay is deliberately all this is for
    // Phase 1; a toggleable, styled HUD is a pure Compose follow-up with
    // no native changes needed since PlaybackEngine already exposes all
    // five numbers.
    Column(modifier = modifier.background(Color.Black.copy(alpha = 0.5f)).padding(6.dp)) {
        Text("FPS: ${stats.fps.roundToLong()}", color = Color.White)
        Text("Decoder: ${stats.decoderFps.roundToLong()}", color = Color.White)
        Text("Renderer: ${stats.rendererFps.roundToLong()}", color = Color.White)
        Text("Dropped: ${stats.droppedFrames.roundToLong()}", color = Color.White)
        Text("GPU: ${stats.gpuTimeMs}ms", color = Color.White)
    }
}

@Composable
private fun TransportControls(state: EditorUiState, viewModel: EditorViewModel) {
    val durationUs = state.project?.durationUs ?: 0L
    Column(modifier = Modifier.padding(horizontal = 12.dp)) {
        Slider(
            value = if (durationUs > 0) state.currentTimeUs.toFloat() / durationUs else 0f,
            onValueChange = { fraction -> viewModel.onSeek((fraction * durationUs).roundToLong()) },
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = androidx.compose.foundation.layout.Arrangement.Center,
        ) {
            IconButton(onClick = { viewModel.onStepFrame(-1) }) {
                Icon(Icons.Filled.SkipPrevious, contentDescription = "Previous frame")
            }
            IconButton(onClick = { viewModel.onPlayPauseToggle() }) {
                Icon(
                    if (state.playbackState == PlaybackState.PLAYING) Icons.Filled.Pause else Icons.Filled.PlayArrow,
                    contentDescription = "Play/Pause",
                )
            }
            IconButton(onClick = { viewModel.onStepFrame(1) }) {
                Icon(Icons.Filled.SkipNext, contentDescription = "Next frame")
            }
        }
        Text(text = "${formatTimecode(state.currentTimeUs)} / ${formatTimecode(durationUs)}")
    }
}

private fun formatTimecode(timeUs: Long): String {
    val totalMs = timeUs / 1000
    val minutes = totalMs / 60_000
    val seconds = (totalMs / 1000) % 60
    val millis = totalMs % 1000
    return "%02d:%02d.%03d".format(minutes, seconds, millis)
}
