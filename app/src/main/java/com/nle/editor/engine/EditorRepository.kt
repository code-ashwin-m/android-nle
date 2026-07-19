package com.nle.editor.engine

import org.json.JSONArray
import org.json.JSONObject

/**
 * The one object in the Kotlin codebase that owns a [NativeEditorEngine].
 * Every ViewModel (there will eventually be more than one -- timeline,
 * properties, export progress) goes through this repository rather than
 * holding its own reference to [NativeEditorEngine], so there is exactly
 * one native handle for the process's lifetime and one place that decides
 * when to create/destroy it.
 *
 * JSON parsing lives here, not in the ViewModel, so [EditorViewModel] deals
 * only in [UiProject]/[UiTrack]/[UiClip] -- the ViewModel should never need
 * to know the wire format is JSON at all, only that it can ask this
 * repository for "the current project" and get one back.
 */
class EditorRepository {
    private val engine = NativeEditorEngine().also { it.create() }

    fun createProject(name: String, width: Int, height: Int, fps: Double) =
        engine.createProject(name, width, height, fps)

    fun importMedia(uri: String, type: MediaType): Long = engine.importMedia(uri, type.ordinal)

    fun addTrack(type: TrackType): Long = engine.addTrack(type.ordinal)
    fun addClip(trackId: Long, sourceId: Long, timelineStartUs: Long, sourceInUs: Long, sourceOutUs: Long): Long =
        engine.addClip(trackId, sourceId, timelineStartUs, sourceInUs, sourceOutUs)
    fun deleteClip(trackId: Long, clipId: Long, ripple: Boolean) = engine.deleteClip(trackId, clipId, ripple)
    fun splitClip(trackId: Long, clipId: Long, atTimeUs: Long) = engine.splitClip(trackId, clipId, atTimeUs)
    fun trimClipHead(trackId: Long, clipId: Long, newSourceInUs: Long) = engine.trimClipHead(trackId, clipId, newSourceInUs)
    fun trimClipTail(trackId: Long, clipId: Long, newSourceOutUs: Long) = engine.trimClipTail(trackId, clipId, newSourceOutUs)

    fun addEffect(trackId: Long, clipId: Long, type: EffectType, defaultValue: Double): Long =
        engine.addEffect(trackId, clipId, type.ordinal, defaultValue)
    fun setBrightness(trackId: Long, clipId: Long, effectId: Long, value: Double) =
        engine.setBrightness(trackId, clipId, effectId, value)

    fun undo() = engine.undo()
    fun redo() = engine.redo()
    fun canUndo(): Boolean = engine.canUndo()
    fun canRedo(): Boolean = engine.canRedo()

    fun attachPreviewSurface(surface: android.view.Surface) = engine.attachPreviewSurface(surface)
    fun detachPreviewSurface() = engine.detachPreviewSurface()
    fun play() = engine.play()
    fun pause() = engine.pause()
    fun stop() = engine.stop()
    fun seekTo(timeUs: Long) = engine.seekTo(timeUs)
    fun stepFrame(delta: Int) = engine.stepFrame(delta)
    fun setPlaybackRate(rate: Double) = engine.setPlaybackRate(rate)
    fun setPreviewQuality(quality: PreviewQuality) = engine.setPreviewQuality(quality.ordinal)
    fun currentTimeUs(): Long = engine.getCurrentTimeUs()
    fun playbackState(): PlaybackState = PlaybackState.entries[engine.getPlaybackState()]
    fun renderStats(): UiRenderStats = engine.getRenderStats().let {
        UiRenderStats(fps = it[0], decoderFps = it[1], rendererFps = it[2], droppedFrames = it[3], gpuTimeMs = it[4])
    }

    fun startExport(outputPath: String, width: Int, height: Int, bitrateBps: Int, fps: Double): Boolean =
        engine.startExport(outputPath, width, height, bitrateBps, fps)
    fun exportProgress(): Float = engine.getExportProgress()
    fun isExporting(): Boolean = engine.isExporting()
    fun cancelExport() = engine.cancelExport()

    /** Pulls the latest project tree from native and parses it into [UiProject]. */
    fun currentProjectSnapshot(): UiProject? {
        val json = engine.getProjectSnapshotJson()
        if (json == "null") return null
        val obj = JSONObject(json)
        return UiProject(
            name = obj.getString("name"),
            widthPx = obj.getInt("widthPx"),
            heightPx = obj.getInt("heightPx"),
            fps = obj.getDouble("fps"),
            durationUs = obj.getLong("durationUs"),
            tracks = obj.getJSONArray("tracks").map(::parseTrack),
        )
    }

    fun dispose() {
        engine.detachPreviewSurface()
        engine.destroy()
    }

    private fun parseTrack(obj: JSONObject) = UiTrack(
        id = obj.getLong("id"),
        type = TrackType.entries[obj.getInt("type")],
        muted = obj.getBoolean("muted"),
        hidden = obj.getBoolean("hidden"),
        clips = obj.getJSONArray("clips").map(::parseClip),
    )

    private fun parseClip(obj: JSONObject) = UiClip(
        id = obj.getLong("id"),
        sourceId = obj.getLong("sourceId"),
        timelineStartUs = obj.getLong("timelineStartUs"),
        durationUs = obj.getLong("durationUs"),
        sourceInUs = obj.getLong("sourceInUs"),
        sourceOutUs = obj.getLong("sourceOutUs"),
        effects = obj.getJSONArray("effects").map(::parseEffect),
    )

    private fun parseEffect(obj: JSONObject): UiEffect {
        val propsJson = obj.getJSONObject("properties")
        val props = mutableMapOf<String, Double>()
        for (key in propsJson.keys()) props[key] = propsJson.getDouble(key)
        return UiEffect(id = obj.getLong("id"), type = EffectType.entries[obj.getInt("type")], properties = props)
    }

    private inline fun <T> JSONArray.map(transform: (JSONObject) -> T): List<T> =
        (0 until length()).map { transform(getJSONObject(it)) }
}
