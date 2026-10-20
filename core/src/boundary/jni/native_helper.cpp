/** @file boundary/jni/native_helper.cpp
 *  Thin JNI wrapper over backend/cve_2026_43284/lib/helper.
 */
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <new>

#include "backend/cve_2026_43284/lib/helper.hpp"

namespace {
using HelperError = ghostlock::backend::cve_2026_43284::lib::HelperError;
thread_local char g_last_error[160] = {0};

void remember(const HelperError &error) noexcept {
    std::snprintf(g_last_error, sizeof(g_last_error), "%s errno=%d status=%d",
                  error.stage != nullptr ? error.stage : "helper", error.err, error.status);
}
}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeHelper_nativeFadviseDrop(
        JNIEnv *env, jclass, jstring path) {
    if (env == nullptr || path == nullptr) return JNI_FALSE;
    const char *name = env->GetStringUTFChars(path, nullptr);
    if (name == nullptr) return JNI_FALSE;
    HelperError error{};
    const bool ok = ghostlock::backend::cve_2026_43284::lib::gl_fadvise_drop(name, &error);
    env->ReleaseStringUTFChars(path, name);
    if (!ok) remember(error);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeHelper_nativeRead(
        JNIEnv *env, jclass, jstring path, jlong offset) {
    if (env == nullptr || path == nullptr) return nullptr;
    const char *name = env->GetStringUTFChars(path, nullptr);
    if (name == nullptr) return nullptr;
    std::uint8_t out[16] = {};
    HelperError error{};
    const bool ok = ghostlock::backend::cve_2026_43284::lib::gl_helper_read(
        name, static_cast<off_t>(offset), out, &error);
    env->ReleaseStringUTFChars(path, name);
    if (!ok) {
        remember(error);
        return nullptr;
    }
    jbyteArray result = env->NewByteArray(16);
    if (result != nullptr) env->SetByteArrayRegion(result, 0, 16, reinterpret_cast<jbyte *>(out));
    return result;
}


extern "C" JNIEXPORT jboolean JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeHelper_nativeTriggerOrphan(
        JNIEnv *, jclass, jint delay_ms) {
    HelperError error{};
    const bool ok = ghostlock::backend::cve_2026_43284::lib::gl_trigger_orphan(
        static_cast<unsigned>(delay_ms), &error);
    if (!ok) remember(error);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeHelper_nativeLastError(JNIEnv *env, jclass) {
    return env->NewStringUTF(g_last_error);
}
