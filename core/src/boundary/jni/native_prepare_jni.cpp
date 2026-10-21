/** Native prepare 的 JNI 薄适配：handle + params blob 进，result blob 出。 */
#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "platform/native_prepare_43499.hpp"

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativePrepareJni_create(JNIEnv *, jclass) {
    return reinterpret_cast<jlong>(ghostlock::platform::native43499::prepare_create());
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_ghostlock_app_engine_ktstep_NativePrepareJni_run(
    JNIEnv *env, jclass, jlong handle, jbyteArray params) {
    if (handle == 0 || params == nullptr) return nullptr;
    const jsize n = env->GetArrayLength(params);
    if (n <= 0) return nullptr;
    jbyte *bytes = env->GetByteArrayElements(params, nullptr);
    if (bytes == nullptr) return nullptr;
    ghostlock::platform::native43499::PrepareParams parsed;
    std::string error;
    const bool ok = ghostlock::platform::native43499::parse_prepare(
        reinterpret_cast<const std::uint8_t *>(bytes), static_cast<std::size_t>(n), parsed, error);
    env->ReleaseByteArrayElements(params, bytes, JNI_ABORT);
    if (!ok) return nullptr;
    ghostlock::platform::native43499::PrepareResult result;
    auto *handle_ptr = reinterpret_cast<ghostlock::platform::native43499::PrepareHandle *>(handle);
    if (ghostlock::platform::native43499::prepare_run(handle_ptr, parsed, result) != 0) return nullptr;
    std::uint8_t encoded[160] = {};
    if (!ghostlock::platform::native43499::encode_prepare_result(result, encoded, sizeof(encoded))) {
        return nullptr;
    }
    jbyteArray out = env->NewByteArray(static_cast<jsize>(sizeof(encoded)));
    if (out == nullptr) return nullptr;
    env->SetByteArrayRegion(out, 0, static_cast<jsize>(sizeof(encoded)),
                            reinterpret_cast<const jbyte *>(encoded));
    return out;
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativePrepareJni_word(
    JNIEnv *, jclass, jlong handle, jint index) {
    std::uint64_t value = 0U;
    if (!ghostlock::platform::native43499::prepare_word(
            reinterpret_cast<const ghostlock::platform::native43499::PrepareHandle *>(handle),
            static_cast<std::uint32_t>(index), value)) {
        return -1;
    }
    return static_cast<jlong>(value);
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativePrepareJni_wordsBase(JNIEnv *, jclass, jlong handle) {
    return static_cast<jlong>(ghostlock::platform::native43499::prepare_words_base(
        reinterpret_cast<const ghostlock::platform::native43499::PrepareHandle *>(handle)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativePrepareJni_payloadBase(JNIEnv *, jclass, jlong handle) {
    return static_cast<jlong>(ghostlock::platform::native43499::prepare_payload_base(
        reinterpret_cast<const ghostlock::platform::native43499::PrepareHandle *>(handle)));
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_ghostlock_app_engine_ktstep_NativePrepareJni_close(JNIEnv *, jclass, jlong handle) {
    ghostlock::platform::native43499::prepare_destroy(
        reinterpret_cast<ghostlock::platform::native43499::PrepareHandle *>(handle));
    return 0;
}
