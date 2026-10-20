/** @file boundary/jni/native_raw_esp.cpp
 *  Thin JNI wrapper: only endpoint lifetime and Java array marshalling.
 *  All page-cache syscalls live in backend/cve_2026_43284/lib/raw_esp.
 */
#include <jni.h>

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <new>

#include "backend/cve_2026_43284/lib/raw_esp.hpp"

namespace {
using Endpoint = ghostlock::backend::cve_2026_43284::lib::RawEspEndpoint;
using RawError = ghostlock::backend::cve_2026_43284::lib::RawEspError;
thread_local char g_last_error[160] = {0};

Endpoint *from_handle(jlong handle) noexcept {
    return reinterpret_cast<Endpoint *>(static_cast<std::intptr_t>(handle));
}

void remember(const RawError &error) noexcept {
    std::snprintf(g_last_error, sizeof(g_last_error), "%s errno=%d",
                  error.stage != nullptr ? error.stage : "raw", error.err);
}
}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeRawEsp_nativeOpen(
        JNIEnv *, jclass, jint spi, jint encap_port, jint sender_port) {
    auto *endpoint = new (std::nothrow) Endpoint();
    if (endpoint == nullptr) return 0;
    RawError error{};
    if (!ghostlock::backend::cve_2026_43284::lib::gl_esp_open(
            static_cast<std::uint32_t>(spi), static_cast<std::uint16_t>(encap_port),
            static_cast<std::uint16_t>(sender_port), endpoint, &error)) {
        remember(error);
        delete endpoint;
        return 0;
    }
    return static_cast<jlong>(reinterpret_cast<std::intptr_t>(endpoint));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeRawEsp_nativeWriteBlock(
        JNIEnv *env, jclass, jlong handle, jstring path, jlong offset, jbyteArray iv, jint sequence) {
    Endpoint *endpoint = from_handle(handle);
    if (endpoint == nullptr || env == nullptr || path == nullptr || iv == nullptr) {
        std::snprintf(g_last_error, sizeof(g_last_error), "raw-args");
        return JNI_FALSE;
    }
    if (env->GetArrayLength(iv) != 16) {
        std::snprintf(g_last_error, sizeof(g_last_error), "raw-iv-size");
        return JNI_FALSE;
    }
    const char *name = env->GetStringUTFChars(path, nullptr);
    if (name == nullptr) {
        std::snprintf(g_last_error, sizeof(g_last_error), "raw-path");
        return JNI_FALSE;
    }
    std::uint8_t iv_bytes[16] = {};
    env->GetByteArrayRegion(iv, 0, 16, reinterpret_cast<jbyte *>(iv_bytes));
    const ghostlock::backend::cve_2026_43284::lib::RawEspWrite request{
        name, static_cast<off_t>(offset), iv_bytes, static_cast<std::uint32_t>(sequence),
    };
    RawError error{};
    const bool ok = ghostlock::backend::cve_2026_43284::lib::gl_esp_write_block(
        *endpoint, request, &error);
    env->ReleaseStringUTFChars(path, name);
    if (!ok) remember(error);
    return ok ? JNI_TRUE : JNI_FALSE;
}


extern "C" JNIEXPORT jboolean JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeRawEsp_nativeWriteBlockViaHelper(
        JNIEnv *env, jclass, jlong handle, jstring path, jlong offset, jbyteArray iv,
        jint sequence, jstring helper_path) {
    Endpoint *endpoint = from_handle(handle);
    if (endpoint == nullptr || env == nullptr || path == nullptr || iv == nullptr || helper_path == nullptr) {
        std::snprintf(g_last_error, sizeof(g_last_error), "raw-helper-args");
        return JNI_FALSE;
    }
    if (env->GetArrayLength(iv) != 16) {
        std::snprintf(g_last_error, sizeof(g_last_error), "raw-helper-iv-size");
        return JNI_FALSE;
    }
    const char *name = env->GetStringUTFChars(path, nullptr);
    const char *helper = env->GetStringUTFChars(helper_path, nullptr);
    if (name == nullptr || helper == nullptr) {
        if (name != nullptr) env->ReleaseStringUTFChars(path, name);
        if (helper != nullptr) env->ReleaseStringUTFChars(helper_path, helper);
        std::snprintf(g_last_error, sizeof(g_last_error), "raw-helper-chars");
        return JNI_FALSE;
    }
    std::uint8_t iv_bytes[16] = {};
    env->GetByteArrayRegion(iv, 0, 16, reinterpret_cast<jbyte *>(iv_bytes));
    const ghostlock::backend::cve_2026_43284::lib::RawEspWrite request{
        name, static_cast<off_t>(offset), iv_bytes, static_cast<std::uint32_t>(sequence),
    };
    RawError error{};
    const bool ok = ghostlock::backend::cve_2026_43284::lib::gl_esp_write_block_via_helper(
        *endpoint, request, helper, &error);
    env->ReleaseStringUTFChars(path, name);
    env->ReleaseStringUTFChars(helper_path, helper);
    if (!ok) remember(error);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeRawEsp_nativeLastError(JNIEnv *env, jclass) {
    return env->NewStringUTF(g_last_error);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ghostlock_app_jni_cve43284_NativeRawEsp_nativeClose(JNIEnv *, jclass, jlong handle) {
    Endpoint *endpoint = from_handle(handle);
    if (endpoint == nullptr) return;
    ghostlock::backend::cve_2026_43284::lib::gl_esp_close(endpoint);
    delete endpoint;
}
