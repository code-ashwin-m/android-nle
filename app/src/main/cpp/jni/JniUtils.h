// JniUtils.h
//
// Every native thread this engine owns (render thread, decoder thread) is
// a plain std::thread, not one created by the JVM -- so none of them have
// a JNIEnv* by default. The *only* reason any of them ever need one is the
// single documented exception described in decode/Decoder.h: constructing
// a android.graphics.SurfaceTexture, which has no native-only construction
// path before attaching a SurfaceTexture that already exists. This header
// is the narrow, reusable utility that makes "attach this native thread to
// the JVM just long enough to construct that one object" safe and
// consistent, rather than each call site rolling its own AttachCurrentThread
// bookkeeping.
//
// JNI_OnLoad (implemented in jni_bridge.cpp) is what populates g_javaVm --
// this header only reads it.

#pragma once

#include <jni.h>

namespace nle {

void SetJavaVM(JavaVM* vm);

// Returns a JNIEnv valid on the calling thread, attaching the thread to the
// JVM first if it isn't already. Safe to call repeatedly from the same
// long-lived native thread (render thread, decoder thread) -- subsequent
// calls are cheap no-ops once attached. Threads that call this are
// expected to live for the process lifetime (this engine never detaches
// them), which avoids a use-after-detach hazard that would exist if a
// short-lived thread attached and detached around a single call.
JNIEnv* GetJniEnvForCurrentThread();

}  // namespace nle
