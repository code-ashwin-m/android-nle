package com.nle.editor.engine

/**
 * Every enum here mirrors a C++ enum passed across JNI as a plain `jint`.
 * "Kotlin only mirrors the state exposed by C++" applies to these types
 * too: this file defines *labels* for values C++ owns, not independent
 * state. If a case is ever added to [nle::TrackType] (core/Types.h) etc.,
 * it must be added here in the same ordinal position, or the two sides
 * silently disagree about what an `Int` means.
 */
enum class TrackType { VIDEO, AUDIO, ADJUSTMENT, STICKER, TEXT }

enum class MediaType { VIDEO, IMAGE, AUDIO }

enum class EffectType { BRIGHTNESS }

enum class PlaybackState { STOPPED, PLAYING, PAUSED, SEEKING }

enum class PreviewQuality { FULL, HALF, QUARTER }

/**
 * Data classes below are the UI-facing mirror of the native object graph
 * (core/Project.h, Timeline.h, Track.h, Clip.h). They are rebuilt from
 * scratch on every native change notification -- see
 * [com.nle.editor.viewmodel.EditorViewModel] -- rather than incrementally
 * patched, which is the simplest way to guarantee Kotlin's view can never
 * drift from what C++ actually holds. For Phase 1's data volumes (a
 * handful of tracks/clips) this is cheap; if timeline size ever makes full
 * rebuilds a measured bottleneck, the fix is a native diff/patch call, not
 * partial mutation of these data classes from multiple call sites.
 */
data class UiProject(
    val name: String,
    val widthPx: Int,
    val heightPx: Int,
    val fps: Double,
    val durationUs: Long,
    val tracks: List<UiTrack>,
)

data class UiTrack(
    val id: Long,
    val type: TrackType,
    val muted: Boolean,
    val hidden: Boolean,
    val clips: List<UiClip>,
)

data class UiClip(
    val id: Long,
    val sourceId: Long,
    val timelineStartUs: Long,
    val durationUs: Long,
    val sourceInUs: Long,
    val sourceOutUs: Long,
    val effects: List<UiEffect>,
)

data class UiEffect(
    val id: Long,
    val type: EffectType,
    val properties: Map<String, Double>,
)

data class UiRenderStats(
    val fps: Float,
    val decoderFps: Float,
    val rendererFps: Float,
    val droppedFrames: Float,
    val gpuTimeMs: Float,
)
