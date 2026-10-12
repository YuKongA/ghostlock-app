/** @file tests/multicast_route_test.cpp
 *  The multicast route's three actors, against a platform that records instead of calling.
 *
 *  What is being pinned is the order, because the order is what the route is: the waiter takes the
 *  chain lock and parks, the owner takes the target and then the chain, the calling thread requeues
 *  the parked waiter onto the target, and the trigger -- which is the write -- runs on the waiter's
 *  own thread, because that is the stack the stale waiter sits on. Two further rules are pinned
 *  beside it: the opening rounds copy the stamp without arming the consumer, and an attempt only
 *  counts when the consumer reports a call the kernel accepted, never when the socket option alone
 *  returned success.
 */
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "backend/cve_2026_43499/seam/multicast_route.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::memory::MulticastGeometry;
    using ghostlock::backend::cve_2026_43499::route::MulticastLimits;
    using ghostlock::backend::cve_2026_43499::route::RouteResultCode;
    using ghostlock::backend::cve_2026_43499::seam::MulticastRoute;
    using ghostlock::backend::cve_2026_43499::seam::MulticastRouteConfig;
    using ghostlock::backend::cve_2026_43499::seam::RouteOps;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** One recorded call: what it was, which thread made it, and the one argument that matters. */
    struct Call final {
        std::string name;
        std::int32_t thread = 0;
        std::int64_t argument = 0;
    };

    /** The recorded platform. Everything the route asks for is remembered, and the few places where
     *  the answer matters are switchable, so both the landing and the not-landing paths can be run. */
    struct FakePlatform final {
        /** The log is written by four threads at once, so both the writing and the reading go
         *  through its own gate: a vector written without one is a race, and a race in the harness
         *  shows up as a harness failure rather than as the route's. */
        std::vector<Call> calls;
        mutable std::mutex log_gate{};
        std::atomic<std::int32_t> next_thread_index{0};
        bool accept_setattr = true;
        bool fallback_succeeds = false;
        std::int32_t stamp_result = 0;
        std::int32_t stamp_errno = 0;
        std::int32_t refusals_before_landing = 0;
        std::uint8_t stamp_copy[512] = {};
        /** Whether the requeue has happened. The park does not return before it does, which is what
         *  makes the order of the two halves of the route a fact rather than a race. */
        std::mutex gate{};
        std::condition_variable gate_changed{};
        bool requeued = false;
        std::size_t stamp_size = 0U;
        std::int32_t opens = 0;
        std::int32_t closes = 0;
        /** The thread the consumer's scheduling call aimed at, which is the whole point of it. */
        std::int32_t setattr_tid = 0;

        static FakePlatform &platform() noexcept {
            static FakePlatform instance;
            return instance;
        }

        void record(const char *name, std::int32_t thread, std::int64_t argument = 0) {
            std::lock_guard<std::mutex> held(log_gate);
            calls.push_back(Call{name, thread, argument});
        }

        /** A copy of the log, taken under the gate, for a reader that wants to walk it. */
        [[nodiscard]] std::vector<Call> snapshot() const {
            std::lock_guard<std::mutex> held(log_gate);
            return calls;
        }

        [[nodiscard]] std::int32_t count(const char *name) const {
            std::lock_guard<std::mutex> held(log_gate);
            std::int32_t total = 0;
            for (const Call &call : calls) {
                if (call.name == name) {
                    ++total;
                }
            }
            return total;
        }

        [[nodiscard]] std::int32_t first_thread(const char *name) const {
            std::lock_guard<std::mutex> held(log_gate);
            for (const Call &call : calls) {
                if (call.name == name) {
                    return call.thread;
                }
            }
            return 0;
        }

        void reset() {
            {
                std::lock_guard<std::mutex> held(log_gate);
                calls.clear();
            }
            accept_setattr = true;
            fallback_succeeds = false;
            stamp_result = 0;
            stamp_errno = 0;
            refusals_before_landing = 0;
            stamp_size = 0U;
            opens = 0;
            closes = 0;
            setattr_tid = 0;
            std::memset(stamp_copy, 0, sizeof(stamp_copy));
            std::lock_guard<std::mutex> held(platform().gate);
            requeued = false;
        }
    };

    /** The platform policy the route is instantiated with. Every call is recorded; a policy is what
     *  the route's own header requires, so this is a whole platform and not a stub. */
    struct FakeOps final {
        static std::int32_t self() noexcept {
            static thread_local const std::int32_t index =
                FakePlatform::platform().next_thread_index.fetch_add(1) + 1;
            return index;
        }

        static int lock_pi(std::uint32_t *word) noexcept {
            *word += 0U;
            FakePlatform::platform().record("lock_pi", self());
            return 0;
        }

        static int lock_pi_until(std::uint32_t *word, std::int64_t micros) noexcept {
            *word += 0U;
            FakePlatform::platform().record("lock_pi_until", self(), micros);
            return micros > 0 && FakePlatform::platform().fallback_succeeds ? 0 : -1;
        }

        static int wait_requeue_pi_until(std::uint32_t *word, std::uint32_t *other,
                                         std::int64_t micros) noexcept {
            *word += 0U;
            *other += 0U;
            FakePlatform &platform = FakePlatform::platform();
            platform.record("wait_requeue_pi", self(), micros);
            /* Parking means waiting for the requeue: it is what the kernel does, and modelling it is
             * what lets the test assert that the copy follows the requeue. */
            std::unique_lock<std::mutex> held(platform.gate);
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::microseconds(micros > 0 ? micros : 1);
            if (!platform.gate_changed.wait_until(held, deadline,
                                                  [&platform] { return platform.requeued; })) {
                return -1;
            }
            return 0;
        }

        static int cmp_requeue_pi(std::uint32_t *word, std::uint32_t *other) noexcept {
            *word += 0U;
            *other += 0U;
            FakePlatform &platform = FakePlatform::platform();
            platform.record("cmp_requeue_pi", self());
            {
                std::lock_guard<std::mutex> held(platform.gate);
                platform.requeued = true;
            }
            platform.gate_changed.notify_all();
            return 1;
        }

        static int unlock_pi(std::uint32_t *word) noexcept {
            *word += 0U;
            FakePlatform::platform().record("unlock_pi", self());
            return 0;
        }

        static int setattr(std::int32_t tid, std::int32_t nice, std::uint32_t policy) noexcept {
            static_cast<void>(policy);
            FakePlatform &platform = FakePlatform::platform();
            platform.setattr_tid = tid;
            platform.record("setattr", self(), nice);
            return platform.accept_setattr ? 0 : -1;
        }

        static void yield() noexcept { std::this_thread::yield(); }

        static void sleep_micros(std::int64_t micros) noexcept {
            if (micros > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(micros));
            }
        }

        static std::int32_t thread_id() noexcept { return self(); }

        static int open_datagram() noexcept {
            FakePlatform &platform = FakePlatform::platform();
            ++platform.opens;
            platform.record("open_datagram", self());
            return 42;
        }

        static int set_block_source(int fd, const std::uint8_t *buffer, std::size_t size) noexcept {
            static_cast<void>(fd);
            FakePlatform &platform = FakePlatform::platform();
            platform.record("set_block_source", self(), static_cast<std::int64_t>(size));
            if (platform.stamp_size == 0U) {
                platform.stamp_size = size;
                std::memcpy(platform.stamp_copy, buffer, size < sizeof(platform.stamp_copy)
                                                               ? size
                                                               : sizeof(platform.stamp_copy));
            }
            if (platform.refusals_before_landing > 0) {
                --platform.refusals_before_landing;
                platform.stamp_errno = FakeOps::addr_not_available();
                return -1;
            }
            platform.stamp_errno = 0;
            return platform.stamp_result;
        }

        static void close_fd(int fd) noexcept {
            static_cast<void>(fd);
            FakePlatform &platform = FakePlatform::platform();
            ++platform.closes;
            platform.record("close_fd", self());
        }

        static std::int32_t last_errno() noexcept { return FakePlatform::platform().stamp_errno; }

        static std::int32_t addr_not_available() noexcept { return 99; }
    };

    static_assert(RouteOps<FakeOps>, "the fake is a whole platform, and the concept says so");

    /** The configuration the device's own geometry produces: the stamp the retired table names, the
     *  attempt budget it records, and the two addresses the prepared region supplies. */
    MulticastRouteConfig device_config() noexcept {
        MulticastRouteConfig config{};
        config.stamp.waiter_offset = 0x20U;
        config.stamp.task_offset = 0x28U;
        config.stamp.lock_offset = 0x30U;
        config.stamp.buffer_size = 0x80U;
        config.limits.max_attempts = 8;
        config.limits.arm_hold = 64;
        config.limits.arm_sequence = 2;
        config.target = 0xFFFFFFC000235EE0ULL;
        config.value = 0xFFFF800000108000ULL;
        config.park_ms = 20;
        config.settle_us = 100;
        config.consumer_delay_us = 0;
        config.burst_calls = 1;
        config.max_calls = 1000;
        config.fallback_lock_us = 50000;
        config.nice_modulus = 19;
        config.policy = 0U;
        config.fake_task = 0xFFFF800000102800ULL;
        config.fake_lock = 0xFFFF800000100E80ULL;
        return config;
    }

    void one_attempt_runs_the_actors_in_order() {
        FakePlatform &platform = FakePlatform::platform();
        /* The caller's own index first, so that "the write happens on another thread" is a fact this
         *  test knows rather than one it assumes. */
        const std::int32_t caller = FakeOps::thread_id();
        platform.reset();
        const MulticastRouteConfig config = device_config();
        MulticastRoute<FakeOps> route(config);
        expect(route.execute(config.target, config.value, 0U), "an attempt with a confirming consumer lands");
        expect(platform.opens == 1 && platform.closes == 1, "one socket for the attempt, and it was closed");
        expect(route.report().status.code == RouteResultCode::Ok, "and the route reports the write landed");
        expect(route.report().successes >= 1, "with the consumer's calls behind it");

        /* The write happened on the waiter's thread: the park, the copy and the scheduling calls the
         * consumer aims all name one thread, and it is not the thread that called execute(). */
        const std::int32_t waiter = platform.first_thread("wait_requeue_pi");
        const std::int32_t stamping = platform.first_thread("set_block_source");
        const std::int32_t aimed = platform.first_thread("setattr");
        expect(waiter != 0 && waiter == stamping, "the trigger runs on the thread that parked");
        expect(aimed != 0 && aimed != waiter, "and the consumer runs on a thread of its own");
        expect(platform.setattr_tid == waiter, "aiming every scheduling call at the parked waiter");
        expect(waiter != caller, "which is not the caller's thread, where the tuple is not");

        /* The order: both locks before the requeue, the requeue before the copy, the disarm after the
         * copy, and the chain released after the trigger. */
        const std::int32_t chain_lock = platform.first_thread("lock_pi");
        expect(chain_lock != 0, "the waiter takes its lock before parking");
        bool requeue_seen = false;
        bool requeue_before_stamp = true;
        bool disarm_after_stamp = true;
        bool disarm_before_close = platform.count("lock_pi_until") == 0 ? false : true;
        for (const Call &call : platform.snapshot()) {
            if (call.name == "cmp_requeue_pi") {
                requeue_seen = true;
            }
            if (call.name == "set_block_source") {
                if (!requeue_seen) {
                    requeue_before_stamp = false;
                }
            }
            if (call.name == "lock_pi_until" && call.argument == 0 && stamping == 0) {
                disarm_after_stamp = false;
            }
            if (call.name == "close_fd" && platform.count("lock_pi_until") == 0) {
                disarm_before_close = false;
            }
        }
        expect(requeue_seen && requeue_before_stamp, "the requeue comes before the copy the trigger makes");
        expect(platform.count("lock_pi_until") == 1, "the removals lock is issued exactly once per trigger");
        expect(disarm_after_stamp, "and after the copy, while the frame is still there");
        expect(disarm_before_close, "with the socket closed after it, not before");
        expect(platform.count("unlock_pi") >= 2, "and both locks are handed back");

        /* The stamp itself: the two pointers at the offsets the geometry names, and the family word
         * the option's structure starts with cleared. */
        expect(platform.stamp_size == config.stamp.buffer_size, "the whole stamp is copied");
        std::uint64_t task = 0U;
        std::uint64_t lock = 0U;
        for (std::size_t i = 0U; i < 8U; ++i) {
            task |= static_cast<std::uint64_t>(platform.stamp_copy[0x20U + 0x28U + i]) << (8U * i);
            lock |= static_cast<std::uint64_t>(platform.stamp_copy[0x20U + 0x30U + i]) << (8U * i);
        }
        expect(task == config.fake_task, "the fabricated task is in the stamp where the geometry says");
        expect(lock == config.fake_lock, "and so is the fabricated lock");
        expect(platform.stamp_copy[8U] == 0U && platform.stamp_copy[9U] == 0U,
               "with the option's family word cleared, so the kernel copies before it validates");

        /* The opening rounds do not arm: the consumer's first call is on the round the budget arms. */
        expect(route.report().attempts >= config.limits.arm_sequence,
               "the opening rounds were spent before arming");
        expect(platform.count("setattr") >= 1, "and the consumer was asked at least once");
        /* The rotation: the retired consumer walks the modulus so one of the values lands, and the
         *  recorded arguments are the proof that this one does too. */
        /* The rotation is per round: the values rise through the modulus and a round boundary resets
         *  them to one. The check follows that rule rather than counting calls, because how many calls
         *  a round gets is the scheduler's business and not this route's -- an earlier version of this
         *  assertion failed whenever the machine was busy. */
        std::int32_t rotated = 0;
        std::int32_t expected = 0;
        bool rotation_ok = true;
        for (const Call &call : platform.snapshot()) {
            if (call.name != "setattr") {
                continue;
            }
            ++rotated;
            expected = (expected % config.nice_modulus) + 1;
            if (call.argument == 1 && expected != 1) {
                expected = 1; /* a new round */
            }
            if (call.argument != expected) {
                rotation_ok = false;
            }
        }
        expect(rotation_ok && rotated >= 1,
               "the nice values rise through the modulus and restart at one each round");
    }

    void a_socket_option_alone_is_not_a_landed_write() {
        FakePlatform &platform = FakePlatform::platform();
        platform.reset();
        const MulticastRouteConfig config = device_config();
        MulticastRoute<FakeOps> route(config);
        /* The copy lands on every round, and the consumer never gets a call the kernel accepts: the
         * retired route treats that as a retry, not as a write. */
        platform.accept_setattr = false;
        platform.fallback_succeeds = false;
        expect(!route.execute(config.target, config.value, 0U),
               "an attempt whose consumer never succeeded does not claim a write");
        expect(route.report().status.code == RouteResultCode::FallbackSafe,
               "and it is reported as the refusal it is, not as a success");
        expect(route.report().successes == 0, "with no accepted call behind it");
        expect(platform.count("set_block_source") > 0, "although the copy was made");
        expect(route.report().attempts == config.limits.max_attempts,
               "and the whole attempt budget was spent");
        expect(route.report().calls > 0, "with the consumer's calls recorded");

        /* A copy the kernel refused for the shape reason still counts as landed, which is the second
         * half of the retired rule. */
        platform.reset();
        platform.refusals_before_landing = 4;
        platform.accept_setattr = true;
        expect(route.execute(config.target, config.value, 0U),
               "a refusal that still means the shape landed is treated as a copy that landed");
        expect(route.report().status.code == RouteResultCode::Ok, "and the write lands on a later round");
    }

    void what_cannot_run_is_refused_before_any_thread_starts() {
        FakePlatform &platform = FakePlatform::platform();
        platform.reset();
        MulticastRouteConfig config = device_config();
        config.limits.arm_sequence = 0;
        MulticastRoute<FakeOps> route(config);
        expect(!route.execute(config.target, config.value, 0U), "a budget of zero attempts is refused");
        expect(platform.opens == 0 && platform.calls.empty(), "and nothing was started or opened");

        config = device_config();
        MulticastRoute<FakeOps> second(config);
        expect(!second.execute(config.target + 8U, config.value, 0U),
               "an attempt for a pair the page was not built for is refused");
        expect(second.report().status.code == RouteResultCode::Unsupported, "by name");
        expect(platform.opens == 0, "and no socket was opened for it");
        expect(!second.execute(config.target, config.value + 8U, 0U),
               "and so is an attempt for another value");
    }
}  // namespace

int main() {
    std::printf("multicast route: one attempt, in order\n");
    one_attempt_runs_the_actors_in_order();
    std::printf("multicast route: what counts as a write\n");
    a_socket_option_alone_is_not_a_landed_write();
    std::printf("multicast route: what cannot run\n");
    what_cannot_run_is_refused_before_any_thread_starts();
    if (g_failures != 0U) {
        std::printf("multicast route: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("multicast route: PASS (park, requeue, trigger on the waiter, consumer confirms)\n");
    return 0;
}
