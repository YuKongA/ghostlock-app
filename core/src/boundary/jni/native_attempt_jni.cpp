/** @file boundary/jni/native_attempt_jni.cpp
 *  N2 C++ actor batch 的 JNI 薄适配：参数 blob 进、结果 blob 出；不做 route/重试/uid 判定。
 */
#include <jni.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>

#include "platform/native_attempt_43499.hpp"

namespace {
    constexpr jlong kHandle = 1;

    [[nodiscard]] jbyteArray answer_blob(JNIEnv *env, const std::uint8_t *data, jsize size) {
        jbyteArray out = env->NewByteArray(size);
        if (out == nullptr) return nullptr;
        env->SetByteArrayRegion(out, 0, size, reinterpret_cast<const jbyte *>(data));
        return out;
    }
}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeAttemptJni_create(JNIEnv *, jclass, jint) {
    return kHandle;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeAttemptJni_attempt(
    JNIEnv *env, jclass, jlong handle, jbyteArray params) {
    if (handle != kHandle || params == nullptr) return nullptr;
    const jsize length = env->GetArrayLength(params);
    if (length <= 0) return nullptr;
    jbyte *const bytes = env->GetByteArrayElements(params, nullptr);
    if (bytes == nullptr) return nullptr;
    ghostlock::platform::native43499::AttemptParams parsed;
    std::string error;
    const bool ok = ghostlock::platform::native43499::parse_attempt(
        reinterpret_cast<const std::uint8_t *>(bytes), static_cast<std::size_t>(length), parsed, error);
    env->ReleaseByteArrayElements(params, bytes, JNI_ABORT);
    if (!ok) return nullptr;

    ghostlock::platform::native43499::AttemptResult result;
    if (ghostlock::platform::native43499::run_attempt(parsed, result) != 0) return nullptr;
    std::uint8_t encoded[48] = {};
    if (!ghostlock::platform::native43499::encode_result(result, encoded, sizeof(encoded))) {
        return nullptr;
    }
    return answer_blob(env, encoded, static_cast<jsize>(sizeof(encoded)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeAttemptJni_close(JNIEnv *, jclass, jlong handle) {
    return handle == kHandle ? 0 : -EINVAL;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeAttemptJni_spawnChild(
    JNIEnv *, jclass, jlong, jbyteArray) {
    return -ENOSYS;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeAttemptJni_childCommand(
    JNIEnv *, jclass, jlong, jint, jbyteArray) {
    return -ENOSYS;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativeAttemptJni_childFinish(
    JNIEnv *, jclass, jlong, jint) {
    return -ENOSYS;
}
