package com.nle.editor.engine

import android.view.Surface

/**
 * The entire JNI surface of the engine. Every `external fun` here is a
 * direct, unmodified forward to `jni_bridge.cpp`, which itself forwards to
 * exactly one [EditorEngine][../../../../cpp/engine/EditorEngine.h] call --
 * per the spec, "No editor logic inside Kotlin," this class contains zero
 * business logic, not even simple derived values. If a function here ever
 * grows an `if` statement that isn't purely about JNI type marshalling,
 * that's a sign the logic belongs in C++ instead.
 *
 * [handle] is an opaque pointer (reinterpreted as a Long) to a native
 * EditorEngine instance. Every other class in the Kotlin codebase talks to
 * the engine through [EditorRepository], never through this class directly
 * -- this class's only reason to exist is to be the one place `external
 * fun` declarations live, so the JNI method signatures Kotlin and C++ must
 * agree on are declared exactly once.
 */
class NativeEditorEngine {
    private var handle: Long = 0L

    fun create() {
        handle = nativeCreateEngine()
    }

    fun destroy() {
        if (handle != 0L) nativeDestroyEngine(handle)
        handle = 0L
    }

    // ---- Project ----------------------------------------------------------

    fun createProject(name: String, width: Int, height: Int, fps: Double) =
        nativeCreateProject(handle, name, width, height, fps)

    fun renameProject(name: String) = nativeRenameProject(handle, name)

    /** Full JSON snapshot of the current project tree; see EditorState.kt. */
    fun getProjectSnapshotJson(): String = nativeGetProjectSnapshotJson(handle)

    // ---- Media Panel --------------------------------------------------------

    /** @return native MediaSourceId */
    fun importMedia(uri: String, mediaType: Int): Long = nativeImportMedia(handle, uri, mediaType)

    // ---- Timeline -----------------------------------------------------------

    fun addTrack(trackType: Int): Long = nativeAddTrack(handle, trackType)

    fun addClip(trackId: Long, sourceId: Long, timelineStartUs: Long, sourceInUs: Long, sourceOutUs: Long): Long =
        nativeAddClip(handle, trackId, sourceId, timelineStartUs, sourceInUs, sourceOutUs)

    fun deleteClip(trackId: Long, clipId: Long, ripple: Boolean) = nativeDeleteClip(handle, trackId, clipId, ripple)

    fun splitClip(trackId: Long, clipId: Long, atTimeUs: Long) = nativeSplitClip(handle, trackId, clipId, atTimeUs)

    fun trimClipHead(trackId: Long, clipId: Long, newSourceInUs: Long) =
        nativeTrimClipHead(handle, trackId, clipId, newSourceInUs)

    fun trimClipTail(trackId: Long, clipId: Long, newSourceOutUs: Long) =
        nativeTrimClipTail(handle, trackId, clipId, newSourceOutUs)

    // ---- Properties Panel -----------------------------------------------------

    fun addEffect(trackId: Long, clipId: Long, effectType: Int, defaultValue: Double): Long =
        nativeAddEffect(handle, trackId, clipId, effectType, defaultValue)

    fun setBrightness(trackId: Long, clipId: Long, effectId: Long, value: Double) =
        nativeSetBrightness(handle, trackId, clipId, effectId, value)

    // ---- Undo / Redo -----------------------------------------------------------

    fun undo() = nativeUndo(handle)
    fun redo() = nativeRedo(handle)
    fun canUndo(): Boolean = nativeCanUndo(handle)
    fun canRedo(): Boolean = nativeCanRedo(handle)

    // ---- Playback / Preview -----------------------------------------------------

    fun attachPreviewSurface(surface: Surface): Boolean = nativeAttachPreviewSurface(handle, surface)
    fun detachPreviewSurface() = nativeDetachPreviewSurface(handle)

    fun play() = nativePlay(handle)
    fun pause() = nativePause(handle)
    fun stop() = nativeStop(handle)
    fun seekTo(timeUs: Long) = nativeSeekTo(handle, timeUs)
    fun stepFrame(deltaFrames: Int) = nativeStepFrame(handle, deltaFrames)
    fun setPlaybackRate(rate: Double) = nativeSetPlaybackRate(handle, rate)
    fun setPreviewQuality(quality: Int) = nativeSetPreviewQuality(handle, quality)

    fun getCurrentTimeUs(): Long = nativeGetCurrentTimeUs(handle)
    fun getPlaybackState(): Int = nativeGetPlaybackState(handle)

    /** @return [fps, decoderFps, rendererFps, droppedFrames, gpuTimeMs] */
    fun getRenderStats(): FloatArray = nativeGetRenderStats(handle)

    // ---- Export -------------------------------------------------------------------

    fun startExport(outputPath: String, width: Int, height: Int, bitrateBps: Int, fps: Double): Boolean =
        nativeStartExport(handle, outputPath, width, height, bitrateBps, fps)

    fun getExportProgress(): Float = nativeGetExportProgress(handle)
    fun isExporting(): Boolean = nativeIsExporting(handle)
    fun cancelExport() = nativeCancelExport(handle)

    // ---- external declarations, 1:1 with jni_bridge.cpp -----------------------------

    private external fun nativeCreateEngine(): Long
    private external fun nativeDestroyEngine(handle: Long)
    private external fun nativeCreateProject(handle: Long, name: String, width: Int, height: Int, fps: Double)
    private external fun nativeRenameProject(handle: Long, name: String)
    private external fun nativeGetProjectSnapshotJson(handle: Long): String

    private external fun nativeImportMedia(handle: Long, uri: String, mediaType: Int): Long

    private external fun nativeAddTrack(handle: Long, trackType: Int): Long
    private external fun nativeAddClip(
        handle: Long, trackId: Long, sourceId: Long, timelineStart: Long, sourceIn: Long, sourceOut: Long
    ): Long
    private external fun nativeDeleteClip(handle: Long, trackId: Long, clipId: Long, ripple: Boolean)
    private external fun nativeSplitClip(handle: Long, trackId: Long, clipId: Long, atTime: Long)
    private external fun nativeTrimClipHead(handle: Long, trackId: Long, clipId: Long, newSourceIn: Long)
    private external fun nativeTrimClipTail(handle: Long, trackId: Long, clipId: Long, newSourceOut: Long)

    private external fun nativeAddEffect(handle: Long, trackId: Long, clipId: Long, effectType: Int, defaultValue: Double): Long
    private external fun nativeSetBrightness(handle: Long, trackId: Long, clipId: Long, effectId: Long, value: Double)

    private external fun nativeUndo(handle: Long)
    private external fun nativeRedo(handle: Long)
    private external fun nativeCanUndo(handle: Long): Boolean
    private external fun nativeCanRedo(handle: Long): Boolean

    private external fun nativeAttachPreviewSurface(handle: Long, surface: Surface): Boolean
    private external fun nativeDetachPreviewSurface(handle: Long)
    private external fun nativePlay(handle: Long)
    private external fun nativePause(handle: Long)
    private external fun nativeStop(handle: Long)
    private external fun nativeSeekTo(handle: Long, timeUs: Long)
    private external fun nativeStepFrame(handle: Long, deltaFrames: Int)
    private external fun nativeSetPlaybackRate(handle: Long, rate: Double)
    private external fun nativeSetPreviewQuality(handle: Long, quality: Int)
    private external fun nativeGetCurrentTimeUs(handle: Long): Long
    private external fun nativeGetPlaybackState(handle: Long): Int
    private external fun nativeGetRenderStats(handle: Long): FloatArray

    private external fun nativeStartExport(
        handle: Long, outputPath: String, width: Int, height: Int, bitrateBps: Int, fps: Double
    ): Boolean
    private external fun nativeGetExportProgress(handle: Long): Float
    private external fun nativeIsExporting(handle: Long): Boolean
    private external fun nativeCancelExport(handle: Long)

    companion object {
        init {
            System.loadLibrary("nleengine")
        }
    }
}
