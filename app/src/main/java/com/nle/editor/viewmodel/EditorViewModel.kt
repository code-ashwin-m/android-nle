package com.nle.editor.viewmodel

import android.view.Surface
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nle.editor.engine.EditorRepository
import com.nle.editor.engine.EffectType
import com.nle.editor.engine.MediaType
import com.nle.editor.engine.PlaybackState
import com.nle.editor.engine.TrackType
import com.nle.editor.engine.UiProject
import com.nle.editor.engine.UiRenderStats
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * This is the only place in the UI layer that decides *when* to ask native
 * for fresh state, and the only place that turns a user gesture into a
 * repository call -- Compose screens below only read [uiState] and call
 * the `on...` methods here, never [EditorRepository] directly. That keeps
 * "the UI must never directly manipulate video" true at the Kotlin/Compose
 * boundary the same way EditorEngine keeps it true at the JNI boundary.
 *
 * Polling, not push: native has no callback into Kotlin for "state
 * changed" (deliberately -- see EditorState.kt's header comment), so this
 * ViewModel polls on a coroutine at two different rates:
 *   - Playback transport (current time, playback state, render stats)
 *     polls every frame interval, since it changes continuously during
 *     playback and the Preview Panel's timecode/FPS readouts need to
 *     track it smoothly.
 *   - The project snapshot (tracks/clips/effects) only refreshes right
 *     after a mutating call, since timeline structure doesn't change on
 *     its own between user actions.
 */
class EditorViewModel : ViewModel() {
    private val repository = EditorRepository()

    private val _uiState = MutableStateFlow(EditorUiState())
    val uiState: StateFlow<EditorUiState> = _uiState.asStateFlow()

    init {
        repository.createProject(name = "Untitled Project", width = 1080, height = 1920, fps = 30.0)
        refreshProjectSnapshot()
        startTransportPolling()
    }

    // ---- Media Panel --------------------------------------------------------

    fun onImportMedia(uri: String, type: MediaType): Long {
        val sourceId = repository.importMedia(uri, type)
        // Import alone doesn't change the timeline, so no snapshot refresh
        // yet -- the Media Panel's own list is repopulated by the caller
        // from the returned MediaSourceId, which Phase 1 keeps client-side
        // until a "media library" native query is added alongside proxy
        // generation.
        return sourceId
    }

    // ---- Timeline ------------------------------------------------------------

    fun onAddTrack(type: TrackType) {
        repository.addTrack(type)
        refreshProjectSnapshot()
    }

    fun onAddClip(trackId: Long, sourceId: Long, timelineStartUs: Long, sourceInUs: Long, sourceOutUs: Long) {
        repository.addClip(trackId, sourceId, timelineStartUs, sourceInUs, sourceOutUs)
        refreshProjectSnapshot()
    }

    fun onDeleteClip(trackId: Long, clipId: Long, ripple: Boolean) {
        repository.deleteClip(trackId, clipId, ripple)
        refreshProjectSnapshot()
    }

    fun onSplitClipAtPlayhead(trackId: Long, clipId: Long) {
        repository.splitClip(trackId, clipId, _uiState.value.currentTimeUs)
        refreshProjectSnapshot()
    }

    fun onTrimClipHead(trackId: Long, clipId: Long, newSourceInUs: Long) {
        repository.trimClipHead(trackId, clipId, newSourceInUs)
        refreshProjectSnapshot()
    }

    fun onTrimClipTail(trackId: Long, clipId: Long, newSourceOutUs: Long) {
        repository.trimClipTail(trackId, clipId, newSourceOutUs)
        refreshProjectSnapshot()
    }

    fun onUndo() { repository.undo(); refreshProjectSnapshot() }
    fun onRedo() { repository.redo(); refreshProjectSnapshot() }

    // ---- Properties Panel -----------------------------------------------------

    fun onAddBrightnessEffect(trackId: Long, clipId: Long) {
        repository.addEffect(trackId, clipId, EffectType.BRIGHTNESS, defaultValue = 0.0)
        refreshProjectSnapshot()
    }

    /** Fired continuously while the user drags the brightness slider. */
    fun onBrightnessChanged(trackId: Long, clipId: Long, effectId: Long, value: Double) {
        repository.setBrightness(trackId, clipId, effectId, value)
        refreshProjectSnapshot()
    }

    // ---- Preview / Playback ------------------------------------------------------

    fun onPreviewSurfaceReady(surface: Surface) {
        repository.attachPreviewSurface(surface)
    }

    fun onPreviewSurfaceDestroyed() {
        repository.detachPreviewSurface()
    }

    fun onPlayPauseToggle() {
        if (_uiState.value.playbackState == PlaybackState.PLAYING) repository.pause() else repository.play()
    }

    fun onSeek(timeUs: Long) = repository.seekTo(timeUs)
    fun onStepFrame(delta: Int) = repository.stepFrame(delta)
    fun onSetPlaybackRate(rate: Double) = repository.setPlaybackRate(rate)

    // ---- internals --------------------------------------------------------------

    private fun refreshProjectSnapshot() {
        _uiState.value = _uiState.value.copy(project = repository.currentProjectSnapshot())
    }

    private fun startTransportPolling() {
        viewModelScope.launch {
            while (true) {
                _uiState.value = _uiState.value.copy(
                    currentTimeUs = repository.currentTimeUs(),
                    playbackState = repository.playbackState(),
                    renderStats = repository.renderStats(),
                )
                delay(16L)  // ~60Hz UI refresh; independent of the render thread's own pacing
            }
        }
    }

    override fun onCleared() {
        repository.dispose()
        super.onCleared()
    }
}

data class EditorUiState(
    val project: UiProject? = null,
    val currentTimeUs: Long = 0L,
    val playbackState: PlaybackState = PlaybackState.STOPPED,
    val renderStats: UiRenderStats = UiRenderStats(0f, 0f, 0f, 0f, 0f),
)
