// jni_bridge.cpp
//
// "JNI should be extremely thin. Only forward commands... No editor logic
// inside Kotlin." Every function below is a type conversion (jlong handle
// -> EditorEngine*, jstring -> std::string) immediately followed by one
// call into EditorEngine. If you find yourself wanting an if/else, a loop,
// or any computation in this file beyond argument marshalling, that logic
// belongs in EditorEngine or a Command instead -- this file is not the
// place decisions get made.
//
// Handle pattern: Kotlin's NativeEditorEngine holds a jlong that is really
// a reinterpret_cast'd EditorEngine*. This is the standard idiom for
// giving a Kotlin object a native-backed lifetime without the JVM needing
// to know anything about the C++ type -- nativeCreateEngine allocates one
// EditorEngine and hands back its address; every other call receives that
// same jlong back and casts it to use it; nativeDestroyEngine deletes it.
// The engine itself has no idea a JVM exists beyond what JniUtils.h
// exposes to the handful of call sites that need it.

#include <android/native_window_jni.h>
#include <jni.h>

#include "engine/EditorEngine.h"
#include "jni/JniUtils.h"

using nle::ClipId;
using nle::EditorEngine;
using nle::EffectId;
using nle::EffectType;
using nle::MediaSourceId;
using nle::MediaType;
using nle::ProjectSettings;
using nle::TimeUs;
using nle::TrackId;
using nle::TrackType;

namespace {

EditorEngine* Engine(jlong handle) { return reinterpret_cast<EditorEngine*>(handle); }

std::string JStringToStd(JNIEnv* env, jstring s) {
    const char* chars = env->GetStringUTFChars(s, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(s, chars);
    return result;
}

template <typename IdType>
IdType IdFromLong(jlong value) {
    return IdType{static_cast<uint64_t>(value)};
}

}  // namespace

extern "C" {

// ---- Library lifecycle ----------------------------------------------------

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    nle::SetJavaVM(vm);
    return JNI_VERSION_1_6;
}

// ---- Engine / Project lifecycle -------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeCreateEngine(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(new EditorEngine());
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeDestroyEngine(JNIEnv*, jobject, jlong handle) {
    delete Engine(handle);
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeCreateProject(JNIEnv* env, jobject, jlong handle, jstring name,
                                                                   jint width, jint height, jdouble fps) {
    ProjectSettings settings;
    settings.width = width;
    settings.height = height;
    settings.fps = fps;
    Engine(handle)->CreateProject(JStringToStd(env, name), settings);
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeRenameProject(JNIEnv* env, jobject, jlong handle, jstring name) {
    Engine(handle)->RenameProject(JStringToStd(env, name));
}

JNIEXPORT jstring JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeGetProjectSnapshotJson(JNIEnv* env, jobject, jlong handle) {
    return env->NewStringUTF(Engine(handle)->GetProjectSnapshotJson().c_str());
}

// ---- Media Panel ------------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeImportMedia(JNIEnv* env, jobject, jlong handle, jstring uri,
                                                                 jint mediaType) {
    MediaSourceId id = Engine(handle)->ImportMedia(JStringToStd(env, uri), static_cast<MediaType>(mediaType));
    return static_cast<jlong>(id.value);
}

// ---- Timeline mutation ------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeAddTrack(JNIEnv*, jobject, jlong handle, jint trackType) {
    TrackId id = Engine(handle)->AddTrack(static_cast<TrackType>(trackType));
    return static_cast<jlong>(id.value);
}

JNIEXPORT jlong JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeAddClip(JNIEnv*, jobject, jlong handle, jlong trackId,
                                                             jlong sourceId, jlong timelineStart, jlong sourceIn,
                                                             jlong sourceOut) {
    ClipId id = Engine(handle)->AddClip(IdFromLong<TrackId>(trackId), IdFromLong<MediaSourceId>(sourceId),
                                        static_cast<TimeUs>(timelineStart), static_cast<TimeUs>(sourceIn),
                                        static_cast<TimeUs>(sourceOut));
    return static_cast<jlong>(id.value);
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeDeleteClip(JNIEnv*, jobject, jlong handle, jlong trackId,
                                                                jlong clipId, jboolean ripple) {
    Engine(handle)->DeleteClip(IdFromLong<TrackId>(trackId), IdFromLong<ClipId>(clipId), ripple);
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeSplitClip(JNIEnv*, jobject, jlong handle, jlong trackId,
                                                               jlong clipId, jlong atTime) {
    Engine(handle)->SplitClip(IdFromLong<TrackId>(trackId), IdFromLong<ClipId>(clipId), static_cast<TimeUs>(atTime));
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeTrimClipHead(JNIEnv*, jobject, jlong handle, jlong trackId,
                                                                  jlong clipId, jlong newSourceIn) {
    Engine(handle)->TrimClipHead(IdFromLong<TrackId>(trackId), IdFromLong<ClipId>(clipId),
                                  static_cast<TimeUs>(newSourceIn));
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeTrimClipTail(JNIEnv*, jobject, jlong handle, jlong trackId,
                                                                  jlong clipId, jlong newSourceOut) {
    Engine(handle)->TrimClipTail(IdFromLong<TrackId>(trackId), IdFromLong<ClipId>(clipId),
                                  static_cast<TimeUs>(newSourceOut));
}

// ---- Properties Panel --------------------------------------------------------

JNIEXPORT jlong JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeAddEffect(JNIEnv*, jobject, jlong handle, jlong trackId,
                                                               jlong clipId, jint effectType, jdouble defaultValue) {
    EffectId id = Engine(handle)->AddEffect(IdFromLong<TrackId>(trackId), IdFromLong<ClipId>(clipId),
                                             static_cast<EffectType>(effectType), defaultValue);
    return static_cast<jlong>(id.value);
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeSetBrightness(JNIEnv*, jobject, jlong handle, jlong trackId,
                                                                   jlong clipId, jlong effectId, jdouble value) {
    Engine(handle)->SetBrightness(IdFromLong<TrackId>(trackId), IdFromLong<ClipId>(clipId),
                                   IdFromLong<EffectId>(effectId), value);
}

// ---- Undo / Redo --------------------------------------------------------------

JNIEXPORT void JNICALL Java_com_nle_editor_engine_NativeEditorEngine_nativeUndo(JNIEnv*, jobject, jlong handle) {
    Engine(handle)->Undo();
}

JNIEXPORT void JNICALL Java_com_nle_editor_engine_NativeEditorEngine_nativeRedo(JNIEnv*, jobject, jlong handle) {
    Engine(handle)->Redo();
}

JNIEXPORT jboolean JNICALL Java_com_nle_editor_engine_NativeEditorEngine_nativeCanUndo(JNIEnv*, jobject, jlong handle) {
    return Engine(handle)->CanUndo();
}

JNIEXPORT jboolean JNICALL Java_com_nle_editor_engine_NativeEditorEngine_nativeCanRedo(JNIEnv*, jobject, jlong handle) {
    return Engine(handle)->CanRedo();
}

// ---- Playback / Preview -------------------------------------------------------

JNIEXPORT jboolean JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeAttachPreviewSurface(JNIEnv* env, jobject, jlong handle,
                                                                          jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return JNI_FALSE;
    bool ok = Engine(handle)->Playback().AttachPreviewSurface(window);
    // PlaybackEngine/GLContext takes eglCreateWindowSurface's own reference
    // via the native window; release this JNI-acquired reference either
    // way once that call returns.
    ANativeWindow_release(window);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeDetachPreviewSurface(JNIEnv*, jobject, jlong handle) {
    Engine(handle)->Playback().DetachPreviewSurface();
}

JNIEXPORT void JNICALL Java_com_nle_editor_engine_NativeEditorEngine_nativePlay(JNIEnv*, jobject, jlong handle) {
    Engine(handle)->Playback().Play();
}

JNIEXPORT void JNICALL Java_com_nle_editor_engine_NativeEditorEngine_nativePause(JNIEnv*, jobject, jlong handle) {
    Engine(handle)->Playback().Pause();
}

JNIEXPORT void JNICALL Java_com_nle_editor_engine_NativeEditorEngine_nativeStop(JNIEnv*, jobject, jlong handle) {
    Engine(handle)->Playback().Stop();
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeSeekTo(JNIEnv*, jobject, jlong handle, jlong timeUs) {
    Engine(handle)->Playback().SeekTo(static_cast<TimeUs>(timeUs));
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeStepFrame(JNIEnv*, jobject, jlong handle, jint deltaFrames) {
    Engine(handle)->Playback().StepFrame(deltaFrames);
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeSetPlaybackRate(JNIEnv*, jobject, jlong handle, jdouble rate) {
    Engine(handle)->Playback().SetPlaybackRate(rate);
}

JNIEXPORT void JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeSetPreviewQuality(JNIEnv*, jobject, jlong handle, jint quality) {
    Engine(handle)->Playback().SetPreviewQuality(static_cast<nle::PreviewQuality>(quality));
}

JNIEXPORT jlong JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeGetCurrentTimeUs(JNIEnv*, jobject, jlong handle) {
    return static_cast<jlong>(Engine(handle)->Playback().CurrentTime());
}

JNIEXPORT jint JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeGetPlaybackState(JNIEnv*, jobject, jlong handle) {
    return static_cast<jint>(Engine(handle)->Playback().State());
}

JNIEXPORT jfloatArray JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeGetRenderStats(JNIEnv* env, jobject, jlong handle) {
    nle::RenderStats stats = Engine(handle)->Playback().Stats();
    jfloatArray result = env->NewFloatArray(5);
    jfloat values[5] = {static_cast<jfloat>(stats.currentFps), static_cast<jfloat>(stats.decoderFps),
                         static_cast<jfloat>(stats.rendererFps), static_cast<jfloat>(stats.droppedFrames),
                         static_cast<jfloat>(stats.lastGpuTimeMs)};
    env->SetFloatArrayRegion(result, 0, 5, values);
    return result;
}

// ---- Export ---------------------------------------------------------------------

JNIEXPORT jboolean JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeStartExport(JNIEnv* env, jobject, jlong handle, jstring outputPath,
                                                                 jint width, jint height, jint bitrateBps, jdouble fps) {
    nle::EncoderConfig config;
    config.outputPath = JStringToStd(env, outputPath);
    config.width = width;
    config.height = height;
    config.bitrateBps = bitrateBps;
    config.fps = fps;
    return Engine(handle)->StartExport(config) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jfloat JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeGetExportProgress(JNIEnv*, jobject, jlong handle) {
    return Engine(handle)->ExportProgress();
}

JNIEXPORT jboolean JNICALL
Java_com_nle_editor_engine_NativeEditorEngine_nativeIsExporting(JNIEnv*, jobject, jlong handle) {
    return Engine(handle)->IsExporting() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_nle_editor_engine_NativeEditorEngine_nativeCancelExport(JNIEnv*, jobject, jlong handle) {
    Engine(handle)->CancelExport();
}

}  // extern "C"
