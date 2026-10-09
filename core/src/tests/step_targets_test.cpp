/** @file tests/step_targets_test.cpp
 *  Base plus offset, and the refusals that keep a guess out of the write path.
 */
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43499/steps/targets.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::steps::FieldOffsets;
    using ghostlock::backend::cve_2026_43499::steps::step_targets_for;
    using ghostlock::backend::cve_2026_43499::steps::StepTargets;
    using ghostlock::backend::cve_2026_43499::steps::TargetsVerdict;
    using ghostlock::backend::cve_2026_43499::steps::targets_verdict_name;

    std::size_t g_failures = 0U;
    void expect(bool condition, const char *what) {
        if (!condition) { std::printf("FAIL: %s\n", what); ++g_failures; }
    }

    void every_address_is_the_base_plus_its_offset() {
        FieldOffsets offsets{};
        offsets.selinux_enforcing = 0x100U;
        offsets.credential = 0x200U;
        offsets.seccomp_mode = 0x300U;
        StepTargets targets{};
        expect(step_targets_for(0xffff800000100000ULL, offsets, targets) == TargetsVerdict::Ok,
               "a real base and real offsets are accepted");
        expect(targets.selinux_enforcing == 0xffff800000100100ULL, "the first is the sum");
        expect(targets.credential == 0xffff800000100200ULL, "the second is its own sum");
        expect(targets.seccomp_mode == 0xffff800000100300ULL, "and the third too");
    }

    void a_guess_is_refused_and_nothing_is_written() {
        /* All three offsets are real here, so a refusal can only come from the base: a case
         *  that leaves an offset at zero proves the offset rule, not the base one. */
        FieldOffsets offsets{};
        offsets.selinux_enforcing = 0x100U;
        offsets.credential = 0x200U;
        offsets.seccomp_mode = 0x300U;
        StepTargets untouched{};
        untouched.credential = 0xabcU;
        expect(step_targets_for(0U, offsets, untouched) == TargetsVerdict::InputsInvalid,
               "a zero base is refused rather than added");
        expect(untouched.credential == 0xabcU, "and the caller's structure is left alone");

        FieldOffsets missing{};
        missing.selinux_enforcing = 0x100U;
        missing.credential = 0x200U;
        /* seccomp stays zero: one offset short is short. */
        StepTargets second{};
        expect(step_targets_for(0x1000U, missing, second) == TargetsVerdict::InputsInvalid,
               "one missing offset refuses the whole set");
        expect(second.selinux_enforcing == 0U, "and nothing partial is exposed");
    }
}  // namespace

int main() {
    std::printf("step targets: base plus offset\n");
    every_address_is_the_base_plus_its_offset();
    std::printf("step targets: refusals\n");
    a_guess_is_refused_and_nothing_is_written();
    if (g_failures != 0U) { std::printf("step targets: FAIL (%zu)\n", g_failures); return 1; }
    std::printf("step targets: PASS (every address is a sum, and a guess is refused)\n");
    return 0;
}
