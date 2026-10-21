/** @file boundary/jni/native_discover_jni.cpp
 *  Kernelsnitch discover 的 JNI 薄适配：params blob 进，result blob 出；不做 route/uid 判定。
 */
#include <jni.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>

#include "platform/native_discover_43499.hpp"

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeDiscoverJni_discover(
    JNIEnv *env, jclass, jlong handle, jbyteArray params) {
    if (handle != 1 || params == nullptr) return nullptr;
    const jsize length = env->GetArrayLength(params);
    if (length <= 0) return nullptr;
    jbyte *const bytes = env->GetByteArrayElements(params, nullptr);
    if (bytes == nullptr) return nullptr;
    ghostlock::platform::native43499::DiscoverParams parsed;
    std::string error;
    const bool ok = ghostlock::platform::native43499::parse_discover(
        reinterpret_cast<const std::uint8_t *>(bytes), static_cast<std::size_t>(length), parsed, error);
    env->ReleaseByteArrayElements(params, bytes, JNI_ABORT);
    if (!ok) return nullptr;

    ghostlock::platform::native43499::DiscoverResult result;
    if (ghostlock::platform::native43499::run_discover(parsed, result) != 0) return nullptr;
    std::uint8_t encoded[64] = {};
    if (!ghostlock::platform::native43499::encode_discover_result(result, encoded, sizeof(encoded))) {
        return nullptr;
    }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(sizeof(encoded)));
    if (out == nullptr) return nullptr;
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(sizeof(encoded)),
                            reinterpret_cast<const jbyte *>(encoded));
    return out;
}
