/** @file tests/stage_result_test.cpp
 *  The stage outcome, its three values, and the exit code each one means.
 *
 *  The values are pinned in order because the ported tree had them in this order and a consumer that
 *  switches on them should not have to care, but a reordering that changed the meaning of an exit code
 *  should stop the build rather than the device. The exit-code mapping is the reason the type exists.
 */
#include <cstdint>
#include <cstdio>

#include "contract/stage_result.hpp"

namespace {
    using ghostlock::contract::StageResult;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** The exit code a stage outcome means, in one place so a caller cannot invent another. */
    constexpr int exit_code_of(StageResult result) noexcept {
        switch (result) {
            case StageResult::Failed: return 1;
            case StageResult::Continue: return 0;
            case StageResult::Done: return 0;
        }
        return 1;
    }

    void the_three_values_are_the_three_that_were_ported() {
        expect(static_cast<int>(StageResult::Failed) == 0, "failed comes first, as ported");
        expect(static_cast<int>(StageResult::Continue) == 1, "then continue");
        expect(static_cast<int>(StageResult::Done) == 2, "then done");
    }

    void the_exit_codes_are_the_documented_ones() {
        expect(exit_code_of(StageResult::Failed) == 1, "a failure ends the run with one");
        expect(exit_code_of(StageResult::Done) == 0, "a finished diagnostic run is a success");
        expect(exit_code_of(StageResult::Continue) == 0, "and continuing is not a failure");
    }

    void every_value_is_distinct() {
        expect(StageResult::Failed != StageResult::Continue, "no two values are the same");
        expect(StageResult::Continue != StageResult::Done, "and the second pair neither");
        expect(StageResult::Failed != StageResult::Done, "nor the third");
    }
}  // namespace

int main() {
    std::printf("stage result: the values\n");
    the_three_values_are_the_three_that_were_ported();
    std::printf("stage result: the exit codes\n");
    the_exit_codes_are_the_documented_ones();
    std::printf("stage result: distinctness\n");
    every_value_is_distinct();
    if (g_failures != 0U) {
        std::printf("stage result: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("stage result: PASS (three values, exit codes as documented)\n");
    return 0;
}
