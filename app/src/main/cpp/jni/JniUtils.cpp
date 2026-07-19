#include "jni/JniUtils.h"

#include <pthread.h>

namespace nle {

namespace {
JavaVM* g_javaVm = nullptr;

// Ensures each native pthread detaches from the JVM when the *thread*
// exits (not when the engine tears down), which matters because
// PlaybackEngine's render thread and DecoderThread are both long-lived
// std::threads whose underlying pthread destruction should not leak a JVM
// attachment. pthread TLS destructors are the standard mechanism for
// "run this when the thread that called GetJniEnvForCurrentThread exits."
void DetachOnThreadExit(void*) {
    if (g_javaVm) g_javaVm->DetachCurrentThread();
}

pthread_key_t MakeDetachKey() {
    pthread_key_t key;
    pthread_key_create(&key, DetachOnThreadExit);
    return key;
}

pthread_key_t g_detachKey = MakeDetachKey();
}  // namespace

void SetJavaVM(JavaVM* vm) { g_javaVm = vm; }

JNIEnv* GetJniEnvForCurrentThread() {
    if (!g_javaVm) return nullptr;

    JNIEnv* env = nullptr;
    jint result = g_javaVm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (result == JNI_OK) return env;

    if (result == JNI_EDETACHED) {
        JavaVMAttachArgs args{JNI_VERSION_1_6, "nle-native-thread", nullptr};
        if (g_javaVm->AttachCurrentThread(&env, &args) != JNI_OK) {
            return nullptr;
        }
        // Registering a non-null value against g_detachKey is what makes
        // the pthread destructor above actually run on thread exit; the
        // value itself is unused, only its presence matters.
        pthread_setspecific(g_detachKey, reinterpret_cast<void*>(1));
        return env;
    }

    return nullptr;  // JNI_EVERSION or similar unrecoverable error
}

}  // namespace nle
