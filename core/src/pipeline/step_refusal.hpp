#pragma once
/** @file pipeline/step_refusal.hpp
 *  The step verdict vocabulary, spelled once.
 *
 *  A step ends either Ok (the print says won) or with one of these names. They used to be
 *  bare literals scattered through the launcher, which meant a verdict could be spelled two
 *  ways, or spelled nowhere, and no reader could list them. The table is that list.
 *
 *  Names are stable: they appear in device logs and in judgement greps, so a rename is a
 *  wire-visible change.
 */

#include <cstddef>

namespace ghostlock::pipeline {
    /** What a step concluded. */
    enum class StepRefusal : unsigned char {
        Ok = 0,
        StepTargetMissing = 1,
        SessionFrameMissing = 2,
        StepExecutionMissing = 3,
        StepMarkersMissing = 4,
        StepParamsInconsistent = 5,
        StepNoDocument = 6,
        StepUnknown = 7,
        PrepareRegionRefused = 8,
        VictimPipesFailed = 9,
        VictimForkFailed = 10,
        VictimLeakDisagreed = 11,
        CredentialImageMissing = 12,
        CredentialLayoutMissing = 13,
        AliasArithmeticMissing = 14,
        InitTaskOffsetMissing = 15,
        VictimCredOffsetMissing = 16,
        PayloadPageUnmapped = 17,
        W2NotRooted = 18,
        TargetOutsideDirectMap = 19,
    };

    /** The printed spelling of a verdict. Never null. */
    [[nodiscard]] constexpr const char *step_refusal_name(StepRefusal refusal) noexcept {
        switch (refusal) {
            case StepRefusal::Ok: return "won";
            case StepRefusal::StepTargetMissing: return "step-target-missing";
            case StepRefusal::SessionFrameMissing: return "session-frame-missing";
            case StepRefusal::StepExecutionMissing: return "step-execution-missing";
            case StepRefusal::StepMarkersMissing: return "step-markers-missing";
            case StepRefusal::StepParamsInconsistent: return "step-params-inconsistent";
            case StepRefusal::StepNoDocument: return "step-no-document";
            case StepRefusal::StepUnknown: return "step-unknown";
            case StepRefusal::PrepareRegionRefused: return "prepare-region-refused";
            case StepRefusal::VictimPipesFailed: return "victim-pipes-failed";
            case StepRefusal::VictimForkFailed: return "victim-fork-failed";
            case StepRefusal::VictimLeakDisagreed: return "victim-leak-disagreed";
            case StepRefusal::CredentialImageMissing: return "credential-image-missing";
            case StepRefusal::CredentialLayoutMissing: return "credential-layout-missing";
            case StepRefusal::AliasArithmeticMissing: return "alias-arithmetic-missing";
            case StepRefusal::InitTaskOffsetMissing: return "init-task-offset-missing";
            case StepRefusal::VictimCredOffsetMissing: return "victim-cred-offset-missing";
            case StepRefusal::PayloadPageUnmapped: return "payload-page-unmapped";
            case StepRefusal::W2NotRooted: return "w2-not-rooted";
            case StepRefusal::TargetOutsideDirectMap: return "target-outside-direct-map";
        }
        return "step-unknown";
    }

    /** How many verdicts the vocabulary holds. */
    inline constexpr std::size_t kStepRefusalCount = 20U;
}  // namespace ghostlock::pipeline
