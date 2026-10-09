#pragma once
/** @file backend/cve_2026_43499/route/lifecycle.hpp
 *  The route lifecycle: prepare, execute, disarm, destroy -- and nothing else.
 *
 *  A route is a policy with a fixed order of use, and the order is the whole
 *  point: a route that executes before it prepared, or that is disarmed while it
 *  has never run, is not a route that merely misbehaved -- it is a caller that
 *  skipped a step. Every step therefore returns a stage, and every illegal step
 *  is refused by name with the stage unchanged, so a refusal never leaves a
 *  route in a state that looks like progress.
 *
 *  Destroy is the one step allowed from anywhere, and it is idempotent: tearing
 *  down is always legal, because it is what a failing caller falls back to.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499::route {
    /** Where a route is in its life. Zero is "nothing has happened yet". */
    enum class RouteStage : std::uint8_t {
        Idle = 0,
        Prepared = 1,
        Running = 2,
        Disarmed = 3,
        Destroyed = 4,
    };

    /** The four things a caller may ask for, in the order they may be asked. */
    enum class RouteStep : std::uint8_t {
        Prepare = 0,
        Execute = 1,
        Disarm = 2,
        Destroy = 3,
    };

    [[nodiscard]] constexpr const char *route_stage_name(RouteStage stage) noexcept {
        switch (stage) {
            case RouteStage::Idle: return "idle";
            case RouteStage::Prepared: return "prepared";
            case RouteStage::Running: return "running";
            case RouteStage::Disarmed: return "disarmed";
            case RouteStage::Destroyed: return "destroyed";
        }
        return "unknown";
    }

    [[nodiscard]] constexpr const char *route_step_name(RouteStep step) noexcept {
        switch (step) {
            case RouteStep::Prepare: return "prepare";
            case RouteStep::Execute: return "execute";
            case RouteStep::Disarm: return "disarm";
            case RouteStep::Destroy: return "destroy";
        }
        return "unknown";
    }

    /** Why a step was refused. Names come from the single table (UML 3.2). */
    enum class LifecycleVerdict : unsigned char {
        Ok = 0,
        /** The step does not follow from this stage. */
        OutOfOrder = 1,
        /** The route is gone; only a fresh one may be used. */
        Destroyed = 2,
    };

    [[nodiscard]] constexpr const char *lifecycle_verdict_name(LifecycleVerdict verdict) noexcept {
        switch (verdict) {
            case LifecycleVerdict::Ok: return "ok";
            case LifecycleVerdict::OutOfOrder: return "order-violation";
            case LifecycleVerdict::Destroyed: return "order-violation";
        }
        return "unknown";
    }

    /** The stage a step leads to, and whether the step was legal at all. */
    struct RouteTransition final {
        LifecycleVerdict verdict = LifecycleVerdict::OutOfOrder;
        RouteStage stage = RouteStage::Idle;
    };

    /** Apply one step to one stage. A refusal returns the stage unchanged, so
     *  nothing that failed can be mistaken for progress. */
    [[nodiscard]] constexpr RouteTransition route_step(RouteStage stage,

                                                       RouteStep step) noexcept {
        RouteTransition result{};
        result.stage = stage;
        if (stage == RouteStage::Destroyed) {
            /* Tearing down again is legal and changes nothing; anything else on a
             * route that is gone is not a step, it is a mistake. */
            result.verdict = step == RouteStep::Destroy ? LifecycleVerdict::Ok

                                                        : LifecycleVerdict::Destroyed;
            return result;
        }
        switch (step) {
            case RouteStep::Prepare:
                if (stage == RouteStage::Idle) {
                    result.verdict = LifecycleVerdict::Ok;
                    result.stage = RouteStage::Prepared;
                }
                return result;
            case RouteStep::Execute:
                if (stage == RouteStage::Prepared) {
                    result.verdict = LifecycleVerdict::Ok;
                    result.stage = RouteStage::Running;
                }
                return result;
            case RouteStep::Disarm:
                /* A route that prepared and never ran still has to be taken back
                 * down, so preparing is enough to be disarmed. */
                if (stage == RouteStage::Prepared || stage == RouteStage::Running) {
                    result.verdict = LifecycleVerdict::Ok;
                    result.stage = RouteStage::Disarmed;
                }
                return result;
            case RouteStep::Destroy:
                result.verdict = LifecycleVerdict::Ok;
                result.stage = RouteStage::Destroyed;
                return result;
        }
        return result;
    }

    /** True when the stage names a route that has run and has been taken back
     *  down (or never needed to be): the only states a caller may leave behind. */
    [[nodiscard]] constexpr bool stage_is_settled(RouteStage stage) noexcept {
        return stage == RouteStage::Idle || stage == RouteStage::Disarmed ||
               stage == RouteStage::Destroyed;
    }
}  // namespace ghostlock::backend::cve_2026_43499::route
