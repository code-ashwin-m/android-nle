// effect_engine_tests.cpp
//
// Runs on a host machine (g++, no NDK/GPU) -- see build_and_run.sh. This is
// the "build / compile / fix compiler errors / run / verify" step from the
// spec's implementation strategy, applied to every part of Phase 2 that
// doesn't require an actual GLES context: Property/Keyframe/MathTypes, the
// JSON parser, the .effect package loader, and full RenderGraph execution
// with real ColorNode/BlurNode/TransformNode instances against a fake
// GraphicsDevice (see stubs/FakeGraphicsDevice.h) and a real TexturePool
// backed by a host-stubbed Texture (see stubs/Texture_host_stub.cpp).
//
// What this does NOT cover: whether GLES actually draws the intended
// pixels, and whether the JNI bridge / Kotlin side compile against an NDK
// toolchain, which this sandbox has no way to run. See
// docs/ARCHITECTURE.md, "What was verified vs. what wasn't" for the exact
// boundary.

#include <cmath>
#include <iostream>
#include <string>

#include "core/Clip.h"
#include "core/Keyframe.h"
#include "core/PlaybackClock.h"
#include "core/Project.h"
#include "core/Property.h"
#include "core/PropertyBag.h"
#include "core/Track.h"
#include "effects/EffectPackageLoader.h"
#include "effects/EffectRuntime.h"
#include "effects/nodes/ExternalInputNode.h"
#include "render/TexturePool.h"
#include "rendergraph/RenderGraph.h"
#include "rendergraph/nodes/PackagedEffectSlotNode.h"
#include "stubs/FakeGraphicsDevice.h"
#include "util/Json.h"

using namespace nle;

namespace {

int gFailures = 0;
int gChecks = 0;

void Check(bool cond, const char* expr, const char* file, int line) {
    ++gChecks;
    if (!cond) {
        ++gFailures;
        std::cerr << "FAIL: " << expr << " (" << file << ":" << line << ")\n";
    }
}

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)

bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// ---------------------------------------------------------------------
// 1. Universal Property System: Property<T>/KeyframeCurve<T> across the
//    new math types, plus the new Ease* interpolation presets.
// ---------------------------------------------------------------------
void TestPropertySystem() {
    // Scalar, Linear -- the Phase 1 case, unchanged.
    ScalarProperty brightness("brightness", 0.0);
    brightness.AddKeyframe(0, 0.0, InterpolationType::Linear);
    brightness.AddKeyframe(SecondsToUs(1.0), 10.0, InterpolationType::Linear);
    CHECK(Near(brightness.ValueAt(0), 0.0));
    CHECK(Near(brightness.ValueAt(SecondsToUs(0.5)), 5.0));
    CHECK(Near(brightness.ValueAt(SecondsToUs(1.0)), 10.0));

    // Vec2, Linear -- proves Lerp<T> works for a non-scalar with zero
    // changes to Keyframe.h, purely from MathTypes.h's three operators.
    Vec2Property position("position", Vec2{0.0, 0.0});
    position.AddKeyframe(0, Vec2{0.0, 0.0});
    position.AddKeyframe(SecondsToUs(1.0), Vec2{10.0, 20.0});
    Vec2 mid = position.ValueAt(SecondsToUs(0.5));
    CHECK(Near(mid.x, 5.0) && Near(mid.y, 10.0));

    // Color, Linear.
    ColorProperty tint("tint", Color{0, 0, 0, 1});
    tint.AddKeyframe(0, Color{0, 0, 0, 1});
    tint.AddKeyframe(SecondsToUs(1.0), Color{1, 1, 1, 1});
    Color midColor = tint.ValueAt(SecondsToUs(0.5));
    CHECK(Near(midColor.r, 0.5) && Near(midColor.g, 0.5) && Near(midColor.b, 0.5));

    // EaseIn/EaseOut/EaseInOut: endpoints must be exact regardless of
    // easing shape; midpoint must diverge from Linear's midpoint (that's
    // the whole point of an easing curve), and EaseIn should start slower
    // than Linear (so at t=0.5 it should still be below the linear value
    // for a rising curve).
    ScalarProperty eased("eased", 0.0);
    eased.AddKeyframe(0, 0.0, InterpolationType::EaseIn);
    eased.AddKeyframe(SecondsToUs(1.0), 10.0, InterpolationType::EaseIn);
    CHECK(Near(eased.ValueAt(0), 0.0));
    CHECK(Near(eased.ValueAt(SecondsToUs(1.0)), 10.0));
    CHECK(eased.ValueAt(SecondsToUs(0.5)) < 5.0);  // eases in => slower start => below the linear midpoint

    ScalarProperty easedOut("easedOut", 0.0);
    easedOut.AddKeyframe(0, 0.0, InterpolationType::EaseOut);
    easedOut.AddKeyframe(SecondsToUs(1.0), 10.0, InterpolationType::EaseOut);
    CHECK(easedOut.ValueAt(SecondsToUs(0.5)) > 5.0);  // eases out => faster start => above the linear midpoint

    // Hold: value snaps at the NEXT keyframe, unaffected by the new enum
    // values slotting in above/below it.
    ScalarProperty held("held", 0.0);
    held.AddKeyframe(0, 1.0, InterpolationType::Hold);
    held.AddKeyframe(SecondsToUs(1.0), 2.0, InterpolationType::Hold);
    CHECK(Near(held.ValueAt(SecondsToUs(0.9)), 1.0));
    CHECK(Near(held.ValueAt(SecondsToUs(1.0)), 2.0));

    // No keyframes at all -- the documented fast path must still work for
    // every type, not just double.
    Vec2Property staticPos("staticPos", Vec2{3.0, 4.0});
    CHECK(!staticPos.IsAnimated());
    Vec2 v = staticPos.ValueAt(123456);
    CHECK(Near(v.x, 3.0) && Near(v.y, 4.0));
}

// ---------------------------------------------------------------------
// 2. PropertyBag: heterogeneous named properties, typed lookup.
// ---------------------------------------------------------------------
void TestPropertyBag() {
    PropertyBag bag;
    bag.AddScalar("exposure", 0.5);
    bag.AddVec2("shake", Vec2{0.0, 0.0});
    bag.AddColor("tint", Color{1, 1, 1, 1});
    CHECK(bag.Count() == 3);

    auto* exposure = bag.FindTyped<double>("exposure");
    CHECK(exposure != nullptr && Near(exposure->StaticValue(), 0.5));

    auto* shake = bag.FindTyped<Vec2>("shake");
    CHECK(shake != nullptr);

    // Wrong type for an existing name must miss, not crash or silently
    // reinterpret.
    CHECK(bag.FindTyped<double>("shake") == nullptr);
    CHECK(bag.FindTyped<Vec2>("exposure") == nullptr);
    CHECK(bag.FindTyped<double>("doesNotExist") == nullptr);
}

// ---------------------------------------------------------------------
// 3. JSON parser correctness on a small hand-written document covering
//    every value type the loader actually relies on.
// ---------------------------------------------------------------------
void TestJsonParser() {
    std::string doc = R"({
        "id": "com.test.example",
        "version": 2,
        "enabled": true,
        "tags": ["a", "b", "c"],
        "position": {"x": 1.5, "y": -2.25},
        "nested": {"inner": {"value": 42}}
    })";
    std::string error;
    JsonValue root = JsonValue::Parse(doc, &error);
    CHECK(error.empty());
    CHECK(root.Get("id").AsString() == "com.test.example");
    CHECK(Near(root.Get("version").AsNumber(), 2.0));
    CHECK(root.Get("enabled").AsBool(false) == true);
    CHECK(root.Get("tags").AsArray().size() == 3);
    CHECK(root.Get("tags").AsArray()[1].AsString() == "b");
    CHECK(Near(root.Get("position").Get("x").AsNumber(), 1.5));
    CHECK(Near(root.Get("position").Get("y").AsNumber(), -2.25));
    CHECK(Near(root.Get("nested").Get("inner").Get("value").AsNumber(), 42.0));
    CHECK(root.Get("missingKey").IsNull());  // absent key -> Null, not a crash

    // Malformed JSON must fail closed with a message, not throw or return
    // a partially-populated value silently.
    std::string badError;
    JsonValue bad = JsonValue::Parse("{ \"a\": }", &badError);
    CHECK(bad.IsNull());
    CHECK(!badError.empty());
}

// ---------------------------------------------------------------------
// 4. EffectPackageLoader against the real, shipped Dreamy/Cyberpunk
//    packages (assets/effects/) -- not synthetic test fixtures, so this
//    also validates the actual content this build ships.
// ---------------------------------------------------------------------
bool LoadRealPackage(const std::string& assetsRoot, const std::string& packageName, EffectGraphDef* outDef) {
    std::string error;
    bool ok = EffectPackageLoader::LoadFromDirectory(assetsRoot + "/" + packageName, outDef, &error);
    if (!ok) std::cerr << "  (loader error for " << packageName << ": " << error << ")\n";
    return ok;
}

void TestEffectPackageLoader(const std::string& assetsRoot) {
    EffectGraphDef dreamy;
    CHECK(LoadRealPackage(assetsRoot, "Dreamy.effect", &dreamy));
    CHECK(dreamy.manifest.id == "com.nle.dreamy");
    CHECK(dreamy.nodes.size() == 2);
    CHECK(dreamy.connections.size() == 3);
    CHECK(dreamy.parameters.size() == 3);
    CHECK(dreamy.FindParameter("glowRadius") != nullptr);
    CHECK(dreamy.defaultAnimations.size() == 1);
    CHECK(dreamy.defaultAnimations[0].keyframes.size() == 3);
    CHECK(dreamy.defaultAnimations[0].keyframes[1].interpolation == InterpolationType::EaseInOut);

    EffectGraphDef cyberpunk;
    CHECK(LoadRealPackage(assetsRoot, "Cyberpunk.effect", &cyberpunk));
    CHECK(cyberpunk.manifest.id == "com.nle.cyberpunk");
    CHECK(cyberpunk.nodes.size() == 2);
    // Cyberpunk's "shake" parameter is a vec2 -- confirms the loader
    // handles a non-scalar declared parameter end to end, not just the
    // scalar path Dreamy happens to exercise for all its parameters.
    const EffectParameterDef* shake = cyberpunk.FindParameter("shake");
    CHECK(shake != nullptr && shake->type == "vec2");
    CHECK(cyberpunk.defaultAnimations.at(0).keyframes.size() == 5);
    CHECK(cyberpunk.defaultAnimations[0].keyframes[1].interpolation == InterpolationType::Hold);
}

// ---------------------------------------------------------------------
// 5. Full pipeline: EffectRuntime::Instantiate building a real subgraph
//    from a real loaded package, executed through a real RenderGraph
//    against the Fake GraphicsDevice + host-stubbed Texture/TexturePool.
//    This is "effects are data" made concrete and actually run.
// ---------------------------------------------------------------------
void TestFullPipeline(EffectRuntime& runtime, const std::string& packageId, bool expectDrawAtTimeZero) {
    const EffectGraphDef* def = runtime.FindLoaded(packageId);
    CHECK(def != nullptr);
    if (!def) return;

    PropertyBag bag;
    RenderGraph graph;
    auto inputNode = std::make_unique<ExternalInputNode>();
    ExternalInputNode* inputPtr = inputNode.get();
    NodeHandle inputHandle = graph.AddNode(std::move(inputNode));

    NodeHandle outputHandle = runtime.Instantiate(*def, bag, graph, inputHandle);
    graph.SetOutputNode(outputHandle);
    // Instantiate must have populated the bag (via PopulateParameters) --
    // an empty bag here would mean a packaged effect's Properties Panel
    // would have nothing to show.
    CHECK(bag.Count() == def->parameters.size());

    FakeGraphicsDevice device;
    TexturePool pool;
    RenderContext context;
    context.graphicsDevice = &device;
    context.texturePool = &pool;
    context.outputWidth = 100;
    context.outputHeight = 100;
    context.time = 0;
    graph.AttachAll(context);

    Frame seed;
    seed.textureId = 999;
    seed.textureTarget = 0x0DE1;
    seed.width = 100;
    seed.height = 100;
    seed.presentationTimeUs = 0;
    inputPtr->SetFrame(seed);

    Frame result = graph.Execute(context);
    CHECK(result.IsValid());
    if (expectDrawAtTimeZero) {
        CHECK(!device.drawCalls.empty());
    }
}

// ---------------------------------------------------------------------
// 6. The trickiest new piece: PackagedEffectSlotNode's per-clip dynamic
//    dispatch, exercised through a real (minimal) Project/Timeline/Track/
//    Clip -- proves a track can hold one clip with a packaged effect and
//    another with none, and the SAME node instance handles both correctly
//    by looking up the active clip fresh each Process() call, exactly
//    like BrightnessNode always has.
// ---------------------------------------------------------------------
void TestPackagedEffectSlotNode(EffectRuntime& runtime) {
    Project project(ProjectId::Generate(), "Test Project", ProjectSettings{});
    Track* track = project.GetTimeline().AddTrack(TrackType::Video);

    // Clip A: 0s-2s, has the Dreamy packaged effect.
    auto clipA = std::make_unique<Clip>(ClipId::Generate(), MediaSourceId::Generate(), 0, 0, SecondsToUs(2.0));
    Effect& effect = clipA->AddEffect(EffectType::Packaged);
    effect.SetPackageId("com.nle.dreamy");
    const EffectGraphDef* dreamyDef = runtime.FindLoaded("com.nle.dreamy");
    CHECK(dreamyDef != nullptr);
    if (dreamyDef) runtime.PopulateParameters(*dreamyDef, effect.Parameters());
    ClipId clipAId = clipA->Id();
    track->AddClip(std::move(clipA));

    // Clip B: 2s-4s, no effects at all.
    auto clipB = std::make_unique<Clip>(ClipId::Generate(), MediaSourceId::Generate(), SecondsToUs(2.0), 0,
                                         SecondsToUs(2.0));
    track->AddClip(std::move(clipB));

    PackagedEffectSlotNode slot(&project, track->Id(), &runtime);
    FakeGraphicsDevice device;
    TexturePool pool;
    RenderContext context;
    context.graphicsDevice = &device;
    context.texturePool = &pool;
    context.outputWidth = 100;
    context.outputHeight = 100;
    slot.OnAttach(context);

    Frame input;
    input.textureId = 1;
    input.textureTarget = 0x0DE1;
    input.width = 100;
    input.height = 100;

    // At t=0.5s, the active clip is A (Dreamy). glowRadius's default
    // animation is > 0 at t=0.5s, so BlurNode must actually draw --
    // proving the lazily-compiled inner graph really executed.
    context.time = SecondsToUs(0.5);
    Frame outA = slot.Process({input}, context);
    CHECK(outA.IsValid());
    CHECK(!device.drawCalls.empty());
    size_t drawsAfterA = device.drawCalls.size();

    // At t=2.5s, the active clip is B, which has no packaged effect at
    // all -- the slot node must pass the input through completely
    // unchanged (same texture id), and must NOT have run any more draws.
    context.time = SecondsToUs(2.5);
    Frame outB = slot.Process({input}, context);
    CHECK(outB.textureId == input.textureId);
    CHECK(device.drawCalls.size() == drawsAfterA);  // no new draws for a clip with no packaged effect

    // Scrubbing back to t=0.5s must reuse the cached inner graph for clip
    // A (built once, keyed by EffectId) rather than rebuilding it -- this
    // doesn't change the *result*, but confirms GetOrBuildCompiled's cache
    // path doesn't throw or misbehave on a second lookup.
    context.time = SecondsToUs(0.9);
    Frame outAAgain = slot.Process({input}, context);
    CHECK(outAAgain.IsValid());
    (void)clipAId;
}

}  // namespace

int main(int argc, char** argv) {
    std::string assetsRoot = argc > 1 ? argv[1] : "../app/src/main/assets/effects";

    TestPropertySystem();
    TestPropertyBag();
    TestJsonParser();
    TestEffectPackageLoader(assetsRoot);

    EffectRuntime runtime;
    std::string error;
    bool dreamyLoaded = runtime.LoadPackage(assetsRoot + "/Dreamy.effect", &error) != nullptr;
    CHECK(dreamyLoaded);
    if (!dreamyLoaded) std::cerr << "  (EffectRuntime::LoadPackage error: " << error << ")\n";
    bool cyberpunkLoaded = runtime.LoadPackage(assetsRoot + "/Cyberpunk.effect", &error) != nullptr;
    CHECK(cyberpunkLoaded);
    if (!cyberpunkLoaded) std::cerr << "  (EffectRuntime::LoadPackage error: " << error << ")\n";

    if (dreamyLoaded) TestFullPipeline(runtime, "com.nle.dreamy", /*expectDrawAtTimeZero=*/true);
    if (cyberpunkLoaded) {
        // At t=0, Cyberpunk's ColorNode has non-default contrast/saturation
        // (0.4 / 0.5) so it draws, even though TransformNode's shake is
        // {0,0} at t=0 and skips its own draw -- expectDrawAtTimeZero
        // checks "at least one node drew", which ColorNode alone satisfies.
        TestFullPipeline(runtime, "com.nle.cyberpunk", /*expectDrawAtTimeZero=*/true);
    }
    if (dreamyLoaded) TestPackagedEffectSlotNode(runtime);

    std::cout << gChecks - gFailures << "/" << gChecks << " checks passed.\n";
    if (gFailures > 0) {
        std::cerr << gFailures << " FAILURE(S).\n";
        return 1;
    }
    std::cout << "All checks passed.\n";
    return 0;
}
