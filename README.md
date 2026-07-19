# Android NLE — Phase 1 Architectural Foundation

This is a from-scratch scaffold for a professional, extensible non-linear
video editor on Android, built around a **native C++20 engine** that is
the single source of truth for all editor state, with a **Jetpack Compose**
UI that only mirrors that state and forwards commands to it.

This document explains the architecture and the reasoning behind it.
Nearly every source file also carries a header comment explaining *why*
it's shaped the way it is — read those alongside this file; this README
is the map, the files are the territory.

## What's actually implemented vs. what's scaffolded

Being direct about this matters more than it might seem, because this
codebase's whole value is in the architecture being *right*, not in every
box being checked.

**Fully implemented, real logic, matches the Phase 1 milestone:**
- The entire core data model (Project/Timeline/Track/Clip/MediaSource/Effect/Property/Keyframe)
- Keyframe interpolation (Linear/Bezier/Hold) with binary-search time lookup
- The Command pattern + Undo/Redo stack, including drag-gesture coalescing
- The Render Graph abstraction (generic DAG, topological execution) and all Phase 1 nodes (Decoder source, Color Convert, Brightness, Composite, Output)
- The GraphicsDevice abstraction over OpenGL ES (the Vulkan-readiness seam)
- Texture pooling
- The NDK MediaCodec/MediaExtractor decoder with async callbacks, keyframe-seek, flush, loop-on-EOS
- The NDK MediaCodec surface-input encoder + MediaMuxer
- The lock-free SPSC ring buffer and the FrameQueue built on it
- The JNI bridge (thin, 1:1 forwarding) and the Kotlin mirror layer (NativeEditorEngine → EditorRepository → EditorViewModel → Compose)
- The four required Compose panels (Preview, Timeline, Properties, Media)

**Deliberately stubbed, with the exact wiring point documented in a comment:**
- `EditorEngine::StartExport` — the *pieces* (Encoder, EncoderOutputNode, a second GLContext) all exist; the ExportEngine class that assembles them into a running export loop on its own thread is the natural next file, not written here because it is almost entirely repetition of `PlaybackEngine`'s loop, and writing it well needs a real device to validate timing against.
- `ProjectStore` (serialization of Project to/from a `.nleproj` file) — `Project` is a pure in-memory model by design specifically so this can be added without touching it.
- The Media Panel's asset list is kept client-side in Kotlin for now (see `MediaBrowserPanel.kt`) rather than queried from native, since there's no persisted media table yet — that arrives naturally with thumbnail/proxy generation.

Nothing above is a "TODO left for later because it was hard" — each is
left exactly at the seam the architecture was built to make possible,
and the comment at each seam says what the next piece looks like.

## Why C++ owns everything

> "The C++ engine is the single source of truth... Kotlin only mirrors
> the state exposed by C++."

Concretely, this rules out a specific, common failure mode in
Kotlin/native hybrid apps: Kotlin holding its own copy of "what clips are
on the timeline" that gets updated by hand alongside native calls, which
reliably drifts out of sync within a few features. Instead:

- `EditorEngine` (C++) is the only owner of `Project`.
- `EditorRepository` (Kotlin) asks `EditorEngine` for a full JSON snapshot
  (`GetProjectSnapshotJson`) after every mutating call and *replaces*
  Kotlin's entire `UiProject` tree with the parsed result.
- There is no code path where Kotlin computes or stores derived editor
  state that C++ doesn't already have.

This is deliberately a full-snapshot-and-replace design rather than an
incremental diff protocol. At Phase 1's data volumes (a handful of
tracks/clips) that's cheap and eliminates an entire class of sync bugs;
if profiling on real timelines ever shows this is a bottleneck, the fix
is a native diff/patch call, not giving Kotlin its own mutable state back.

## Why a Render Graph, not a fixed pipeline

This is the architectural center of the whole project, and the reason is
almost entirely about **preview/export parity**. `RenderGraph` is a
generic DAG executor with no concept of "preview" or "export" — it just
runs `RenderNode`s in topological order. `PlaybackEngine` builds one graph
(`DecoderSource → ColorConvert → Brightness → Composite`) and attaches
either a `PreviewOutputNode` or an `EncoderOutputNode` as the terminal
node. Every upstream node runs byte-for-byte identically either way. This
is what guarantees exported video matches what was previewed, rather than
"should match, assuming both code paths were kept in sync by hand."

It also means every future effect (Blur, LUT, Crop, Mask, Transitions,
Blend Modes...) is: implement `RenderNode`, write a shader, wire it into
the graph. `RenderGraph` itself never changes.

## Why Command pattern for every mutation

`CommandStack` has no locking and assumes single-threaded mutation. That's
only safe because `EditorEngine` is the *only* place that touches
`Project` for writes, and every write goes through a `Command`. This
buys:
- Uniform, correct Undo/Redo across structural edits (add/delete/split/trim clip) and value edits (brightness) without hand-writing "how to reverse this" logic scattered through the codebase.
- Drag-gesture coalescing (`CanMergeWith`/`MergeWith`) so a slider drag firing 60 times a second is one undo step, not 60.

## Threading model

```
UI thread (Compose)          — never touches native directly except via NativeEditorEngine calls
   │  JNI calls (synchronous, thin)
   ▼
EditorEngine                 — owns Project + CommandStack; all mutation serialized here
   │
   ├─ PlaybackEngine's render thread   — never blocks on UI/decoder; reads Project state (Property::ValueAt etc.) each frame
   │      └─ RenderGraph::Execute()    — DecoderSourceNode calls into...
   │             └─ Decoder::FrameNear() — reads from a lock-free FrameQueue, never touches the extractor/codec itself
   │
   ├─ DecoderThread (one per open Decoder) — owns AMediaExtractor/AMediaCodec exclusively; async callbacks feed it
   │
   └─ Encoder / export path (stubbed — see above) — mirrors the render thread's structure with an EncoderOutputNode
```

Cross-thread handoffs use a lock-free SPSC ring buffer (`util/SpscRingBuffer.h`)
specifically where there's exactly one producer and one consumer — decoder
thread → render thread. Where a producer is genuinely not single-threaded
by construction (AMediaCodec's own async callback thread), a plain mutex
is used instead (`DecoderThread.cpp`) rather than forcing a lock-free
structure onto a case it wasn't designed for.

## The one place Kotlin does real work

JNI is meant to be "extremely thin," and it is — except for one
documented, narrow exception: constructing an `android.graphics.SurfaceTexture`.
Prior to API 28's `ASurfaceTexture` NDK API, `SurfaceTexture` can only be
*constructed* from Java/Kotlin — there's no pure-native path. `DecoderSourceNode`
handles this by attaching its own native render thread to the JVM
(`jni/JniUtils.h`) and calling back into Java purely to construct that one
object; every actual decision (which decoder to open, when to seek, what
frame to return) stays in C++. See `decode/Decoder.h`'s header comment for
the full reasoning.

## Directory structure

```
app/src/main/cpp/
  include/nle/core/Types.h      — shared time units and strongly-typed IDs
  core/                         — Project, Timeline, Track, Clip, MediaSource, Property, Keyframe, PlaybackClock
  command/                      — Command, CommandStack, and every concrete Command
  rendergraph/                  — RenderGraph, RenderNode, Frame, and every node (decoder source, color convert, brightness, composite, output)
  render/                       — GLContext (EGL), Texture, TexturePool, GraphicsDevice abstraction, OpenGLRenderer
  decode/                       — Decoder, DecoderThread, FrameQueue (NDK MediaExtractor/MediaCodec)
  encode/                       — Encoder (NDK MediaCodec surface input + MediaMuxer)
  playback/                     — PlaybackEngine (render thread, graph assembly, frame pacing, stats)
  engine/                       — EditorEngine (the facade JNI calls into)
  util/                         — SpscRingBuffer
  jni/                          — jni_bridge.cpp (thin forwarding), JniUtils (JVM attach helper)

app/src/main/java/com/nle/editor/
  engine/                       — NativeEditorEngine (external fun), EditorState (UI-facing mirrors), EditorRepository
  viewmodel/                    — EditorViewModel (polling, event translation)
  ui/                           — EditorScreen + preview/timeline/properties/media panels
```

## Building

This is a standard Gradle + CMake Android project.

```
./gradlew assembleDebug
```

Two things to know before that succeeds on a fresh checkout:
1. **No launcher icon is included** — add one under `res/mipmap-*` or
   remove `android:icon` from the manifest for a first build.
2. **This has not been compiled against a real NDK toolchain in this
   environment** (no network/Android SDK access where this was written).
   The C++ is written directly against real NDK headers
   (`media/NdkMediaCodec.h`, `android/surface_texture.h`, EGL/GLES3) with
   care taken to get call signatures and constant values right, but treat
   the first real build as the point where compiler errors get fixed, not
   as a signal the architecture is wrong if a signature needs a tweak.

`minSdk` is 28, specifically because of the `ASurfaceTexture` NDK
dependency described above — see `app/build.gradle.kts`.

## Roadmap — how future phases attach without refactoring

| Future feature | Where it attaches |
|---|---|
| Contrast, Exposure, Saturation, Opacity, Scale, Rotation, Crop, Mask, Blur | New `EffectType` + new `RenderNode` subclass, copying `BrightnessNode`'s shape |
| Multiple video tracks (already supported structurally) | `CompositeNode` already accepts N inputs; `PlaybackEngine::RebuildGraphIfNeeded` already loops over all video tracks |
| Transitions | A `RenderNode` taking exactly 2 inputs, inserted between two `CompositeNode` inputs for the overlap region |
| Blend modes | A per-input parameter `CompositeNode` reads from the track/clip, not a structural change |
| Adjustment layers | A track type whose "clip" wraps an effect stack applied to the composite of layers beneath it — `Effect`/`Property` are already generic enough to reuse unchanged |
| LUTs | A `RenderNode` sampling a 3D texture, same shape as `BrightnessNode` |
| Audio mixing | A parallel `AudioRenderNode` chain feeding an `AudioMixNode`, running on its own timing domain but sharing `PlaybackClock` |
| Vulkan renderer | A new `GraphicsDevice` implementation; zero `RenderNode` changes |
| Proxy editing | `MediaSource` gains an optional proxy URI; `Decoder::Open` picks it when preview quality is reduced |
| Timeline caching / background rendering | A cache layer in front of `RenderGraph::Execute` keyed by `(graph version, time)`, transparent to nodes |
| Desktop version | `GraphicsDevice`/`Decoder`/`Encoder` are the only platform-facing interfaces; a desktop build swaps their implementations (e.g. FFmpeg-backed decode) behind the same interfaces `RenderNode`s already use |

## A note on scope

This scaffold is a serious architectural foundation, not a finished
product — a real Phase 1 milestone, built the way the spec asked for it
to be built, is genuinely a multi-month effort for a small team even with
this structure in place. What's here is meant to be the thing you build
*on top of* without hitting load-bearing walls later, which is the actual
ask: get the shape right before writing the next thousand lines, not
minimize lines written today.
