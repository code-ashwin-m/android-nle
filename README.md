# Android NLE — Phase 1 Architectural Foundation + Phase 2 (Universal Property System)

This is a from-scratch scaffold for a professional, extensible non-linear
video editor on Android, built around a **native C++20 engine** that is
the single source of truth for all editor state, with a **Jetpack Compose**
UI that only mirrors that state and forwards commands to it.

Phase 2 extends this foundation with the Universal Property System and a
data-driven effect engine (packaged `.effect` files interpreted by generic
nodes, not one hardcoded C++ class per effect) — see "Phase 2" below for
what that means concretely and why it was built the way it was.

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

**Phase 2 additions, fully implemented, real logic, verified by a host test suite (see "Testing without an NDK toolchain" below):**
- The Universal Property System: `Vec2`/`Vec3`/`Color`/`Matrix4` math types and `EaseIn`/`EaseOut`/`EaseInOut` interpolation, added to `Property<T>`/`KeyframeCurve<T>` with **zero changes** to how `Lerp<T>` or the interpolation switch dispatch — see `core/MathTypes.h`
- `PropertyBag` — a heterogeneously-typed, named property container (`core/PropertyBag.h`), used by every packaged effect's parameters
- A hand-rolled JSON reader (`util/Json.h/.cpp`) and the `.effect` package format it parses (manifest/graph/parameters/animations — see "Phase 2: effects are data, not code" below)
- `EffectNodeRegistry` + three real generic nodes (`ColorNode`, `TransformNode`, `BlurNode`) — effects are graphs of these, not hardcoded per-effect C++ classes
- `EffectRuntime` — loads/caches packages and splices a package's node graph into the shared `RenderGraph` (the "Effect Runtime → Graph Builder" pipeline stages)
- `PackagedEffectSlotNode` and `ClipTransformNode` — the per-track nodes that make per-clip effect graphs and per-clip transform animation work within a graph that's rebuilt only on structural edits (see "The per-track vs. per-clip problem" below)
- Adjustment layer tracks (`TrackType::Adjustment`) now actually composite — `PlaybackEngine::RebuildGraphIfNeeded` folds tracks bottom-to-top so an adjustment track's effects apply to the composite beneath it
- Two example packages, `Dreamy.effect` and `Cyberpunk.effect` (`app/src/main/assets/effects/`), proving a second effect needs zero new C++
- `host_tests/` — a host-machine (plain `g++`, no NDK) test suite covering all of the above

**Deliberately stubbed, with the exact wiring point documented in a comment:**
- `EditorEngine::StartExport` — the *pieces* (Encoder, EncoderOutputNode, a second GLContext) all exist; the ExportEngine class that assembles them into a running export loop on its own thread is the natural next file, not written here because it is almost entirely repetition of `PlaybackEngine`'s loop, and writing it well needs a real device to validate timing against.
- `ProjectStore` (serialization of Project to/from a `.nleproj` file) — `Project` is a pure in-memory model by design specifically so this can be added without touching it.
- The Media Panel's asset list is kept client-side in Kotlin for now (see `MediaBrowserPanel.kt`) rather than queried from native, since there's no persisted media table yet — that arrives naturally with thumbnail/proxy generation.
- `GlowNode`, `OverlayNode`, `NoiseNode`, `MaskNode`, `LUTNode`, `ParticleNode` — the remaining node types from the spec's list, each following `BlurNode`/`ColorNode`/`TransformNode`'s exact shape (see `effects/EffectNodeRegistry.cpp`'s header comment)
- The Properties Panel isn't wired to a packaged effect's `PropertyBag` yet — `Effect::Parameters()` exists and is populated (see `EffectRuntime::PopulateParameters`), but `PropertiesPanel.kt` only currently binds to the older per-`EffectType` `ScalarProperty` list
- The JNI bridge doesn't yet expose `EditorEngine::LoadEffectPackage`/`AddPackagedEffect` to Kotlin — both exist and are exercised by `host_tests/`, but no `external fun` calls them yet
- The standalone Effect Builder application, an effects marketplace, downloadable/user-created effects, and the iOS/Desktop/Vulkan ports are unstarted; see the roadmap table for exactly what each attaches to when it is

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

## Phase 2: the Universal Property System

Every animatable value in the editor — a clip's position, an effect's
blur radius, an adjustment layer's exposure — is a `Property<T>`, and
every `Property<T>` is driven by the exact same `KeyframeCurve<T>`
evaluator (`core/Keyframe.h`, unchanged since Phase 1). What Phase 2 adds
is `core/MathTypes.h`: `Vec2`, `Vec3`, `Color`, and `Matrix4`, each
supplying only the three operators (`+`, `-`, `operator*(double)`) that
`Lerp<T>` already needed. No branch anywhere had to learn "how to lerp a
Vec2" — the generic `a + (b - a) * t` from Phase 1 already covered it.
`EaseIn`/`EaseOut`/`EaseInOut` were added to `InterpolationType` the same
way: three more fixed-control-point calls into the same
`CubicBezierEase` function `Bezier` already used.

`PropertyBag` (`core/PropertyBag.h`) is the named, heterogeneous
container these live in for anything that isn't a fixed clip/effect field
— a packaged effect's declared parameters, for instance. It's a
`std::variant` of `Property<T>` instantiations, not a virtual hierarchy,
specifically so a caller that knows the type it wants back
(`FindTyped<Vec2>("shake")`) gets a real `Property<Vec2>*`, not a
type-erased wrapper it then has to unbox anyway.

## Phase 2: effects are data, not code

The spec's requirement was specific: adding a new effect (Dreamy,
Cyberpunk, whatever a future Effect Builder produces) must never mean
writing a new C++ class. Concretely:

```
Playback Clock → Timeline Evaluator → Property Evaluation →
Effect Runtime → Graph Builder → RenderGraph → Renderer
```

- An **`.effect` package** (`app/src/main/assets/effects/Dreamy.effect/`)
  is four JSON files: `manifest.json` (id/name/version), `parameters.json`
  (the effect's declared, animatable parameters), `graph.json` (which
  generic nodes to instantiate and how to wire them), and an optional
  `animations.json` (default keyframes, so an effect visibly does
  something the instant it's added, not just when a user hand-authors
  keyframes for it).
- **`EffectPackageLoader`** (`effects/EffectPackageLoader.h`) parses those
  four files into an `EffectGraphDef` — plain data, no behavior.
- **`EffectNodeRegistry`** (`effects/EffectNodeRegistry.h`) is the *only*
  place a `graph.json` node's `"type": "ColorNode"` string becomes an
  actual C++ constructor call. `Dreamy.effect` and `Cyberpunk.effect`
  reference `ColorNode`/`BlurNode`/`TransformNode` — the exact same three
  classes — with different parameters; see both packages under
  `app/src/main/assets/effects/` for a second effect that required
  editing zero `.h`/`.cpp` files.
- **`EffectRuntime::Instantiate`** (`effects/EffectRuntime.h`) is the
  "Graph Builder": it populates a target `PropertyBag` from the package's
  declared parameters, builds one `RenderNode` per node definition via
  the registry, and wires them together using `RenderGraph`'s existing
  public `AddNode`/`Connect` — no changes to `RenderGraph` itself.

### The per-track vs. per-clip problem

Phase 1's nodes are built **per track**, not per clip:
`PlaybackEngine::RebuildGraphIfNeeded` runs once per structural edit and
each node (e.g. `BrightnessNode`) looks up "whichever clip is active on
this track right now" fresh on every `Process()` call, via
`Track::ClipAt(context.time)`. That works for Brightness because every
clip has at most one Brightness value — the graph's *shape* never
depends on which clip is active, only a number does.

Packaged effects break that assumption: one clip might have Dreamy
(`ColorNode → BlurNode`), the next might have Cyberpunk
(`ColorNode → TransformNode`), and the one after that might have nothing.
That's a difference in graph *topology*, not just parameter values, and
the outer `RenderGraph`'s topology is only supposed to change on
structural edits — not every time the playhead crosses a cut.

`PackagedEffectSlotNode` (`rendergraph/nodes/PackagedEffectSlotNode.h`)
resolves this by owning a small, **embedded** `RenderGraph` per clip
effect instance (lazily built via `EffectRuntime::Instantiate`, cached by
`EffectId`), fed through the new `ExternalInputNode`
(`effects/nodes/ExternalInputNode.h` — a node with no input edges whose
frame is set from outside, right before executing). The outer graph's
shape never changes when the playhead moves between differently-effected
clips; only which cached inner graph gets executed does.
`ClipTransformNode` (`rendergraph/nodes/ClipTransformNode.h`) uses the
same "look up the active clip every frame" shape for a clip's intrinsic
Position/Rotation/Scale, delegating the actual math to `TransformNode`
via `Rebind()` rather than reconstructing a node every frame.

The same per-track node also gives Adjustment Layers a home:
`RebuildGraphIfNeeded` now folds tracks into a running `compositeSoFar`
handle bottom-to-top instead of collecting every video track's output for
one final N-way composite, so a `TrackType::Adjustment` track's
`PackagedEffectSlotNode`/`BrightnessNode` pair runs on the composite of
only the layers beneath it, using `CompositeNode` completely unchanged.

### Existing Phase 1 files Phase 2 modified, and why

The brief was explicit that existing systems shouldn't be redesigned.
Every file below was *extended*, not rewritten — no existing method
signature, behavior, or call site was changed except where noted.

| File | What changed | Why |
|---|---|---|
| `core/Keyframe.h` | Added `EaseIn`/`EaseOut`/`EaseInOut` to `InterpolationType` and three cases to `Evaluate()`'s switch | New presets, reusing the existing `CubicBezierEase`/`Lerp` — `Hold`/`Linear`/`Bezier` untouched |
| `core/Property.h` | Added optional `PropertyMetadata` (category/range/UI hint) and typed aliases (`Vec2Property`, etc.) | Additive: existing `Property(name, default)` constructor and every call site still compiles unchanged |
| `core/Clip.h` | `Effect` gained `packageId_`/`PropertyBag parameters_`; `Clip` gained `Position()`/`Rotation()`/`Scale()` | Packaged effects and clip transform needed somewhere to live; Brightness's existing `AddProperty`/`FindProperty` path is untouched |
| `playback/PlaybackEngine.h/.cpp` | Constructor now takes an `EffectRuntime*`; `RebuildGraphIfNeeded` inserts `PackagedEffectSlotNode`/`ClipTransformNode` per track and folds tracks incrementally (see above); `graphBuilt_` is now `std::atomic<bool>` with a public `InvalidateGraph()` | New per-track nodes need the runtime; incremental folding is what makes Adjustment Layers possible; `InvalidateGraph()` fixes a real Phase 1 gap — see next row |
| `engine/EditorEngine.h/.cpp` | Owns an `EffectRuntime`; added `LoadEffectPackage`/`AddPackagedEffect`; every structural mutation (`AddClip`, `AddEffect`, `Undo`, ...) now calls `playback_->InvalidateGraph()` | **Bug fix, not a feature**: Phase 1's `graphBuilt_` was only ever cleared once, in `AttachPreviewSurface` — a clip or effect added *after* the preview surface was already up would silently never render. Harmless for Brightness (its node re-reads state every frame regardless of graph shape) but a real bug for anything needing a graph-shape change, which packaged effects do. `SetBrightness` deliberately does *not* call `InvalidateGraph()` — see its comment for why. |
| `command/commands/TimelineCommands.h` | Added `AddPackagedEffectCommand` | Follows `AddEffectCommand`'s exact shape; needed so adding a packaged effect is undoable like everything else |
| `app/src/main/cpp/CMakeLists.txt` | Added every new file below | — |

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
  core/                         — Project, Timeline, Track, Clip, MediaSource, Property, PropertyBag, MathTypes, Keyframe, PlaybackClock
  command/                      — Command, CommandStack, and every concrete Command
  rendergraph/                  — RenderGraph, RenderNode, Frame, and every node (decoder source, color convert, brightness, clip transform, packaged-effect slot, composite, output)
  effects/                      — the data-driven effect engine: EffectGraphDef, EffectPackageLoader, EffectNodeRegistry, EffectRuntime, and the generic nodes (Color/Transform/Blur/ExternalInput) packages reference
  render/                       — GLContext (EGL), Texture, TexturePool, GraphicsDevice abstraction, OpenGLRenderer
  decode/                       — Decoder, DecoderThread, FrameQueue (NDK MediaExtractor/MediaCodec)
  encode/                       — Encoder (NDK MediaCodec surface input + MediaMuxer)
  playback/                     — PlaybackEngine (render thread, graph assembly, frame pacing, stats)
  engine/                       — EditorEngine (the facade JNI calls into)
  util/                         — SpscRingBuffer, Json (hand-rolled reader for .effect packages)
  jni/                          — jni_bridge.cpp (thin forwarding), JniUtils (JVM attach helper)

app/src/main/assets/effects/
  Dreamy.effect/, Cyberpunk.effect/  — example packages: manifest.json, graph.json, parameters.json, animations.json

app/src/main/java/com/nle/editor/
  engine/                       — NativeEditorEngine (external fun), EditorState (UI-facing mirrors), EditorRepository
  viewmodel/                    — EditorViewModel (polling, event translation)
  ui/                           — EditorScreen + preview/timeline/properties/media panels

host_tests/                     — host-machine (g++, no NDK) test suite for everything in effects/ and core/'s new pieces; see "Testing without an NDK toolchain" below
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

## Testing without an NDK toolchain

`host_tests/` exists because of the same constraint point 2 above
describes: no Android SDK/NDK/GPU is available in the environment this
was built in. Rather than leave Phase 2's new logic (`Property<T>` over
the new math types, the JSON parser, `EffectPackageLoader`,
`EffectRuntime`, and the real `ColorNode`/`BlurNode`/`TransformNode`
executing through a real `RenderGraph`) completely unverified, it's
exercised on the host machine with plain `g++`:

```
cd host_tests
./build_and_run.sh
```

This works because `RenderNode`, `RenderGraph`, `GraphicsDevice`, and
`TexturePool` were already plain C++ interfaces with no GLES/Android
dependency baked into their headers — only their real *implementations*
(`OpenGLRenderer`, `Texture::CreateEmpty`) call actual GL functions.
`host_tests/stubs/` supplies a `FakeGraphicsDevice` (records draw calls
instead of issuing them) and a host-only `Texture_host_stub.cpp` (hands
out incrementing fake ids instead of calling `glGenTextures`) — compiled
only into the test binary, never into the real Android build. This lets
the test suite construct and run real node instances through a real
graph, not just the GPU-free logic in isolation.

**What this does and doesn't prove:** it verifies every new piece of
logic actually compiles and behaves correctly — property interpolation,
JSON parsing, package loading, graph wiring, and `PackagedEffectSlotNode`
correctly switching between a clip with a packaged effect and one without
using a real two-clip track. It does **not** verify `PlaybackEngine.cpp`,
`EditorEngine.cpp`, or the JNI bridge compile against a real NDK — those
`#include` real `EGL/egl.h`/JNI headers this sandbox doesn't have, the
same limitation the Phase 1 scaffold already disclosed above. Those files'
Phase 2 changes are mechanical wiring (constructing already-tested nodes,
calling already-tested `EffectRuntime` methods) reviewed by hand, not
independently compiled.

## Roadmap — how future phases attach without refactoring

| Future feature | Where it attaches |
|---|---|
| ~~Contrast, Exposure, Saturation, Scale, Rotation~~ | **Done in Phase 2** — `ColorNode`/`TransformNode`, either as packaged-effect parameters or (Position/Rotation/Scale) directly on `Clip` |
| Opacity, Crop | Same shape as Phase 2's `ColorNode`/`TransformNode` — a parameter on the existing node (opacity: alpha multiply in `ColorNode` or `CompositeNode`) or a new one following `TransformNode`'s pattern (crop: same inverse-sampling idea, clamping instead of transforming) |
| Glow, Overlay, Noise, Mask, LUT, Particles | New `RenderNode` subclasses in `effects/nodes/`, registered in `EffectNodeRegistry.cpp` — the exact shape `ColorNode`/`TransformNode`/`BlurNode` already established, not a new mechanism |
| Multiple video tracks (already supported structurally) | `CompositeNode` already accepts N inputs; `PlaybackEngine::RebuildGraphIfNeeded`'s incremental fold (Phase 2) already loops over all video tracks |
| ~~Adjustment layers~~ | **Done in Phase 2** — see "The per-track vs. per-clip problem" above |
| Properties Panel ↔ packaged effects | `Effect::Parameters()` (a `PropertyBag`) is already populated by `EffectRuntime::PopulateParameters` the moment an effect is added — `PropertiesPanel.kt`/`GetProjectSnapshotJson` need to walk it the same way they already walk the per-`EffectType` `ScalarProperty` list |
| Transitions | A `RenderNode` taking exactly 2 inputs, inserted between two `CompositeNode` inputs for the overlap region |
| Blend modes | A per-input parameter `CompositeNode` reads from the track/clip, not a structural change |
| Audio effects, audio automation | Same `Property<T>`/`PropertyBag` machinery Phase 2 built for video parameters — an `AudioRenderNode` reading a `Property<double>` per frame is the same shape as `ColorNode` |
| Camera animation | `TransformNode` applied to the whole composite rather than one clip — see its header comment |
| Effect Builder application | Per the spec's "must use EXACTLY the same runtime": a visual graph editor producing `graph.json`/`parameters.json` that `EffectPackageLoader`/`EffectRuntime` already load unchanged — no new runtime, just a new JSON-producing tool |
| Downloadable effects / marketplace | `EffectRuntime::LoadPackage` already takes an arbitrary directory path; add a download-and-unpack step in front of it |
| User-created effects | Same path as the Effect Builder — any tool that produces a valid `.effect` package works, this runtime doesn't distinguish "official" from "user-made" |
| Audio mixing | A parallel `AudioRenderNode` chain feeding an `AudioMixNode`, running on its own timing domain but sharing `PlaybackClock` |
| Vulkan renderer | A new `GraphicsDevice` implementation; zero `RenderNode`/`EffectNodeRegistry` changes — every node already goes through the `GraphicsDevice` interface, not raw GL calls |
| Proxy editing | `MediaSource` gains an optional proxy URI; `Decoder::Open` picks it when preview quality is reduced |
| Timeline caching / background rendering | A cache layer in front of `RenderGraph::Execute` keyed by `(graph version, time)`, transparent to nodes |
| Desktop version | `GraphicsDevice`/`Decoder`/`Encoder` are the only platform-facing interfaces; a desktop build swaps their implementations (e.g. FFmpeg-backed decode) behind the same interfaces `RenderNode`s already use |
| iOS version | Same interfaces as Desktop; `EffectRuntime`/`EffectPackageLoader`/`EffectNodeRegistry` are already pure C++20 with no Android dependency, so the entire effect engine is portable as-is |

## A note on scope

This scaffold is a serious architectural foundation, not a finished
product — a real Phase 1 milestone, built the way the spec asked for it
to be built, is genuinely a multi-month effort for a small team even with
this structure in place. What's here is meant to be the thing you build
*on top of* without hitting load-bearing walls later, which is the actual
ask: get the shape right before writing the next thousand lines, not
minimize lines written today.
