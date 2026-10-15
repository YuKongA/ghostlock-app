#pragma once
/** @file pipeline/plan_gate.hpp
 *  P4 step 5: plan_gate -- the binary admission decision for one script plan.
 *
 *  Authority: docs/analysis/composition-layer-p4-spec.md section 3.
 *
 *  Position in the chain: after the backend's state_from succeeds (the gate
 *  needs the resolved geometry) and before Backend::run. The verdict is binary:
 *  it never produces an alternative plan and never falls back at run time.
 *
 *  Rejection vocabulary: the ten static rejections of section 3 plus R11's
 *  queue-removed and plan-missing. Precedence used here (documented because the
 *  spec fixes the NAMES, not the priority):
 *    plan-missing -> queue-removed -> geometry-missing -> capability-missing ->
 *    executor-unavailable -> per op [unknown-op -> op-not-available ->
 *    route-not-available -> order-violation -> hash-mismatch -> param-invalid ->
 *    landing-in-pi-window].
 *
 *  Facts (geometry / capabilities / executor) are inputs: they are frozen by the
 *  composition root before the window and the gate only reads them (section 3).
 *  Parameter bounds and required-ness come from contract::ParamSpec alone.
 */
#include <cstddef>
#include <cstdint>

#include "contract/op_registry.hpp"

namespace ghostlock::pipeline {
    /** Why a plan was refused (or Ok). */
    enum class GateReason : unsigned char {
        Ok = 0,
        UnknownOp = 1,
        OpNotAvailable = 2,
        RouteNotAvailable = 3,
        GeometryMissing = 4,
        CapabilityMissing = 5,
        OrderViolation = 6,
        HashMismatch = 7,
        ParamInvalid = 8,
        LandingInPiWindow = 9,
        ExecutorUnavailable = 10,
        QueueRemoved = 11,
        PlanMissing = 12,
    };

    /** Exhaustive switch: a new reason breaks the build here. */
    [[nodiscard]] constexpr const char *gate_reason_name(GateReason reason) noexcept {
        switch (reason) {
            case GateReason::Ok: return "ok";
            case GateReason::UnknownOp: return "unknown-op";
            case GateReason::OpNotAvailable: return "op-not-available";
            case GateReason::RouteNotAvailable: return "route-not-available";
            case GateReason::GeometryMissing: return "geometry-missing";
            case GateReason::CapabilityMissing: return "capability-missing";
            case GateReason::OrderViolation: return "order-violation";
            case GateReason::HashMismatch: return "hash-mismatch";
            case GateReason::ParamInvalid: return "param-invalid";
            case GateReason::LandingInPiWindow: return "landing-in-pi-window";
            case GateReason::ExecutorUnavailable: return "executor-unavailable";
            case GateReason::QueueRemoved: return "queue-removed";
            case GateReason::PlanMissing: return "plan-missing";
        }
        return "unknown";
    }

    /** Fixed-size path so the gate stays allocation-free (section 3). */
    inline constexpr std::size_t kGatePathBytes = 72;

    /** The verdict: ok=false means "refuse and stop", with a named reason. */
    struct GateVerdict final {
        bool ok = false;
        GateReason reason = GateReason::PlanMissing;
        /** 0-based op index; it must match the [i] inside path. */
        std::size_t index = 0;
        /** path syntax: backend.<id> -> .op[<i>] -> .<field>. */
        char path[kGatePathBytes] = {};
    };

    /** One parameter value carried by the plan. */
    struct PlanParam final {
        const char *key;
        std::uint64_t value;
    };

    /** One op invocation in the plan. */
    struct PlanOp final {
        const char *token;
        /** The plan's claim; a mismatch with op_token_hash(token) is refused. */
        std::uint64_t token_hash;
        /** Plan-declared routes; a plan may narrow an op but never widen it. */
        contract::RouteMask route_mask;
        const PlanParam *params;
        std::size_t param_count;
        /** True when the plan puts a landing inside the PI window (refused). */
        bool landing_inside_window;
        /** Prerequisite op position; it must be < this op's position (-1 = none). */
        std::int32_t depends_on;
    };

    /** Everything the gate reads; the composition root fills it before the window. */
    struct PlanInput final {
        const char *backend_id;
        const PlanOp *ops;
        std::size_t op_count;
        bool script_present;
        bool builtin_plan_present;
        /** backend.<id>.queue present in the document (R11: refused). */
        bool queue_present;
        bool geometry_present;
        bool capability_present;
        bool executor_available;
    };

    namespace plan_gate_detail {
        /** Append text at offset; truncation is impossible for our fixed shapes. */
        inline void append(char *out, std::size_t &used, const char *text) noexcept {
            for (std::size_t i = 0; text[i] != '\0' && used + 1 < kGatePathBytes; ++i) {
                out[used++] = text[i];
                out[used] = '\0';
            }
        }

        inline void append_index(char *out, std::size_t &used, std::size_t value) noexcept {
            char digits[24];
            std::size_t n = 0;
            if (value == 0) {
                digits[n++] = '0';
            }
            while (value != 0 && n < sizeof(digits)) {
                digits[n++] = static_cast<char>('0' + (value % 10U));
                value /= 10U;
            }
            while (n > 0) {
                char one[2] = {digits[--n], '\0'};
                append(out, used, one);
            }
        }

        /** Build "backend.<id>" plus optional op/field parts into the verdict. */
        inline void make_path(GateVerdict &verdict, const char *backend_id, bool with_op,
                              std::size_t op_index, const char *field) noexcept {
            std::size_t used = 0;
            verdict.path[0] = '\0';
            append(verdict.path, used, "backend.");
            append(verdict.path, used, backend_id != nullptr ? backend_id : "?");
            if (with_op) {
                append(verdict.path, used, ".op[");
                append_index(verdict.path, used, op_index);
                append(verdict.path, used, "]");
            }
            if (field != nullptr) {
                append(verdict.path, used, ".");
                append(verdict.path, used, field);
            }
        }

        [[nodiscard]] inline GateVerdict refuse(GateReason reason, const char *backend_id,
                                                bool with_op, std::size_t op_index,
                                                const char *field) noexcept {
            GateVerdict verdict{};
            verdict.ok = false;
            verdict.reason = reason;
            verdict.index = with_op ? op_index : 0;
            make_path(verdict, backend_id, with_op, op_index, field);
            return verdict;
        }

        [[nodiscard]] inline bool same_text(const char *a, const char *b) noexcept {
            if (a == nullptr || b == nullptr) {
                return false;
            }
            while (*a != '\0' && *a == *b) {
                ++a;
                ++b;
            }
            return *a == *b;
        }
    }  // namespace plan_gate_detail

    /** The gate: binary admission decision for one plan. */
    [[nodiscard]] inline GateVerdict plan_gate(const PlanInput &in) noexcept {
        using plan_gate_detail::refuse;
        /* 1. no plan at all (or a plan that names nothing) -- refused by name. */
        if ((!in.script_present && !in.builtin_plan_present) || in.op_count == 0) {
            return refuse(GateReason::PlanMissing, in.backend_id, false, 0, nullptr);
        }
        /* 2. R11: the retired queue key must not appear. */
        if (in.queue_present) {
            return refuse(GateReason::QueueRemoved, in.backend_id, false, 0, "queue");
        }
        /* 3. frozen facts (read-only): geometry, capabilities, executor. */
        if (!in.geometry_present) {
            return refuse(GateReason::GeometryMissing, in.backend_id, false, 0, nullptr);
        }
        if (!in.capability_present) {
            return refuse(GateReason::CapabilityMissing, in.backend_id, false, 0, nullptr);
        }
        if (!in.executor_available) {
            return refuse(GateReason::ExecutorUnavailable, in.backend_id, false, 0, nullptr);
        }
        /* 4. per op, in the documented precedence. */
        for (std::size_t i = 0; i < in.op_count; ++i) {
            const PlanOp &plan_op = in.ops[i];
            const contract::OpSpec *spec = contract::op_by_token(plan_op.token);
            if (spec == nullptr) {
                return refuse(GateReason::UnknownOp, in.backend_id, true, i, "token");
            }
            if (contract::op_token_hash(plan_op.token) != plan_op.token_hash) {
                return refuse(GateReason::HashMismatch, in.backend_id, true, i, "token_hash");
            }
            if (!spec->available) {
                return refuse(GateReason::OpNotAvailable, in.backend_id, true, i, nullptr);
            }
            if ((plan_op.route_mask & ~spec->routes) != 0U ||
                (plan_op.route_mask & ~contract::kAvailableRouteMask) != 0U) {
                return refuse(GateReason::RouteNotAvailable, in.backend_id, true, i, "route");
            }
            if (plan_op.depends_on >= static_cast<std::int32_t>(i)) {
                return refuse(GateReason::OrderViolation, in.backend_id, true, i, "depends_on");
            }
            /* parameters: required-ness and bounds come from the table only. */
            for (std::size_t p = 0; p < spec->param_count; ++p) {
                const contract::ParamSpec &ps = spec->params[p];
                const PlanParam *found = nullptr;
                for (std::size_t q = 0; q < plan_op.param_count; ++q) {
                    if (plan_op.params[q].key != nullptr && plan_gate_detail::same_text(plan_op.params[q].key, ps.key)) {
                        found = &plan_op.params[q];
                        break;
                    }
                }
                if (found == nullptr) {
                    if (ps.required) {
                        return refuse(GateReason::ParamInvalid, in.backend_id, true, i, ps.key);
                    }
                    continue;
                }
                if (found->value > ps.max) {
                    return refuse(GateReason::ParamInvalid, in.backend_id, true, i, ps.key);
                }
            }
            /* an undeclared parameter key is refused as well (no silent ignore) */
            for (std::size_t q = 0; q < plan_op.param_count; ++q) {
                bool declared = false;
                for (std::size_t p = 0; p < spec->param_count; ++p) {
                    if (plan_op.params[q].key != nullptr && plan_gate_detail::same_text(plan_op.params[q].key, spec->params[p].key)) {
                        declared = true;
                        break;
                    }
                }
                if (!declared) {
                    return refuse(GateReason::ParamInvalid, in.backend_id, true, i, plan_op.params[q].key);
                }
            }
            if (plan_op.landing_inside_window) {
                return refuse(GateReason::LandingInPiWindow, in.backend_id, true, i, "landing");
            }
        }
        GateVerdict ok{};
        ok.ok = true;
        ok.reason = GateReason::Ok;
        ok.index = 0;
        plan_gate_detail::make_path(ok, in.backend_id, false, 0, nullptr);
        return ok;
    }

    static_assert(static_cast<unsigned char>(GateReason::PlanMissing) == 12, "reason order is frozen");
    static_assert(kGatePathBytes >= 32, "the path buffer must fit backend.<id>.op[<i>].<field>");
}  // namespace ghostlock::pipeline
