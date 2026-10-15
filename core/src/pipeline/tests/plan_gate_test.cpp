/** @file pipeline/tests/plan_gate_test.cpp
 *  P4 milestone B evidence: the twelve named rejections, the binary verdict,
 *  the frozen parameter bounds, and the extreme values of spec section 7.
 *
 *  Build and run: make -C src p4-window-check
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "contract/op_registry.hpp"
#include "pipeline/plan_gate.hpp"

namespace {
    using ghostlock::contract::kAvailableRouteMask;
    using ghostlock::contract::op_token_hash;
    using ghostlock::contract::route_bit;
    using ghostlock::pipeline::GateReason;
    using ghostlock::pipeline::gate_reason_name;
    using ghostlock::pipeline::GateVerdict;
    using ghostlock::pipeline::PlanInput;
    using ghostlock::pipeline::PlanOp;
    using ghostlock::pipeline::PlanParam;
    using ghostlock::pipeline::plan_gate;

    int failures = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++failures;
        }
    }

    /** The path must carry the same op position as verdict.index. */
    bool index_matches_path(const GateVerdict &v) {
        char needle[24];
        std::snprintf(needle, sizeof(needle), ".op[%zu]", v.index);
        return std::string_view(v.path).find(needle) != std::string_view::npos;
    }

    PlanInput base_input(const PlanOp *ops, std::size_t count) {
        PlanInput in{};
        in.backend_id = "cve_2026_43499";
        in.ops = ops;
        in.op_count = count;
        in.script_present = true;
        in.builtin_plan_present = false;
        in.queue_present = false;
        in.geometry_present = true;
        in.capability_present = true;
        in.executor_available = true;
        return in;
    }

    /** The one parameter the table marks required for window.selinux_cred_seccomp. */
    const PlanParam kRequiredAttempts[] = {{"attempts", 1U}};

    PlanOp window_op(const char *token = "window.selinux_cred_seccomp", std::int32_t depends_on = -1) {
        PlanOp op{};
        op.token = token;
        op.token_hash = op_token_hash(token);
        op.route_mask = kAvailableRouteMask;
        if (std::string_view(token) == "window.selinux_cred_seccomp") {
            op.params = kRequiredAttempts;
            op.param_count = 1;
        } else {
            op.params = nullptr;
            op.param_count = 0;
        }
        op.landing_inside_window = false;
        op.depends_on = depends_on;
        return op;
    }

    void case_ok() {
        PlanOp op = window_op();
        const PlanParam params[] = {{"attempts", 8U}, {"nice", 19U}};
        op.params = params;
        op.param_count = 2;
        const PlanInput in = base_input(&op, 1);
        const GateVerdict v = plan_gate(in);
        expect(v.ok, "single op with in-bounds params must pass");
        expect(v.reason == GateReason::Ok, "passing verdict reason must be ok");
        std::printf("case ok: reason=%s path=%s\n", gate_reason_name(v.reason), v.path);
    }

    void case_param_at_bound_then_over() {
        PlanOp op = window_op();
        PlanParam params[] = {{"attempts", 8U}};
        op.params = params;
        op.param_count = 1;
        PlanInput in = base_input(&op, 1);
        expect(plan_gate(in).ok, "param == max must pass");
        params[0].value = 9U;  /* max + 1 */
        const GateVerdict v = plan_gate(in);
        expect(!v.ok && v.reason == GateReason::ParamInvalid, "param > max must be refused");
        expect(v.index == 0 && index_matches_path(v), "param verdict index must match the path");
        std::printf("case param max+1: reason=%s path=%s %s\n", gate_reason_name(v.reason), v.path,
                    index_matches_path(v) ? "index-consistent" : "INDEX-MISMATCH");
    }

    void case_required_missing() {
        PlanOp op = window_op();  /* attempts is required in the table */
        op.params = nullptr;
        op.param_count = 0;
        const PlanInput in = base_input(&op, 1);
        const GateVerdict v = plan_gate(in);
        expect(!v.ok && v.reason == GateReason::ParamInvalid, "missing required param must be refused");
        std::printf("case required missing: reason=%s path=%s\n", gate_reason_name(v.reason), v.path);
    }

    void case_undeclared_param() {
        PlanOp op = window_op();
        const PlanParam params[] = {{"attempts", 1U}, {"nope", 1U}};
        op.params = params;
        op.param_count = 2;
        const PlanInput in = base_input(&op, 1);
        const GateVerdict v = plan_gate(in);
        expect(!v.ok && v.reason == GateReason::ParamInvalid, "undeclared param must be refused");
        std::printf("case undeclared param: reason=%s path=%s\n", gate_reason_name(v.reason), v.path);
    }

    void case_unknown_op() {
        PlanOp op = window_op("window.nope");
        const PlanInput in = base_input(&op, 1);
        const GateVerdict v = plan_gate(in);
        expect(!v.ok && v.reason == GateReason::UnknownOp, "unknown token must be refused");
        std::printf("case unknown op: reason=%s path=%s\n", gate_reason_name(v.reason), v.path);
    }

    void case_op_not_available() {
        PlanOp op = window_op("window.selinux_cred_seccomp_umh");
        const PlanInput in = base_input(&op, 1);
        const GateVerdict v = plan_gate(in);
        expect(!v.ok && v.reason == GateReason::OpNotAvailable, "planned op must be refused");
        std::printf("case op-not-available: reason=%s path=%s\n", gate_reason_name(v.reason), v.path);
    }

    void case_route_not_available() {
        PlanOp op = window_op();
        op.route_mask = kAvailableRouteMask | 0x80000000U;  /* widen beyond the op */
        const PlanInput in = base_input(&op, 1);
        const GateVerdict v = plan_gate(in);
        expect(!v.ok && v.reason == GateReason::RouteNotAvailable, "widened route must be refused");
        std::printf("case route-not-available: reason=%s path=%s\n", gate_reason_name(v.reason), v.path);
    }

    void case_hash_mismatch() {
        PlanOp op = window_op();
        op.token_hash ^= 0x1U;
        const PlanInput in = base_input(&op, 1);
        const GateVerdict v = plan_gate(in);
        expect(!v.ok && v.reason == GateReason::HashMismatch, "stale token hash must be refused");
        std::printf("case hash-mismatch: reason=%s path=%s\n", gate_reason_name(v.reason), v.path);
    }

    void case_order_violation() {
        PlanOp ops[2] = {window_op(), window_op("window.selinux_cred")};
        ops[1].depends_on = 1;  /* must be < 1 */
        const PlanInput in = base_input(ops, 2);
        const GateVerdict v = plan_gate(in);
        expect(!v.ok && v.reason == GateReason::OrderViolation, "self-dependency must be refused");
        std::printf("case order-violation: reason=%s path=%s\n", gate_reason_name(v.reason), v.path);
    }

    void case_landing_in_window() {
        PlanOp ops[2] = {window_op(), window_op("window.selinux_cred", 0)};
        ops[1].landing_inside_window = true;
        const PlanInput in = base_input(ops, 2);
        const GateVerdict v = plan_gate(in);
        expect(!v.ok && v.reason == GateReason::SeamInPiWindow, "landing inside the window must be refused");
        expect(v.index == 1 && index_matches_path(v), "landing verdict index must match the path");
        std::printf("case landing-in-pi-window: reason=%s path=%s index=%zu\n", gate_reason_name(v.reason), v.path,
                    v.index);
    }

    void case_facts_missing() {
        PlanOp op = window_op();
        op.params = nullptr;
        op.param_count = 0;
        PlanInput in = base_input(&op, 1);
        in.geometry_present = false;
        expect(plan_gate(in).reason == GateReason::GeometryMissing, "missing geometry must be refused");
        in = base_input(&op, 1);
        in.capability_present = false;
        expect(plan_gate(in).reason == GateReason::CapabilityMissing, "missing capability must be refused");
        in = base_input(&op, 1);
        in.executor_available = false;
        expect(plan_gate(in).reason == GateReason::ExecutorUnavailable, "missing executor must be refused");
        std::printf("case facts: geometry/capability/executor rejections ok\n");
    }

    void case_queue_and_plan_missing() {
        PlanOp op = window_op();
        PlanInput in = base_input(&op, 1);
        in.queue_present = true;
        const GateVerdict q = plan_gate(in);
        expect(!q.ok && q.reason == GateReason::QueueRemoved, "queue key must be refused");
        expect(std::string_view(q.path) == "backend.cve_2026_43499.queue", "queue path must name the field");
        in = base_input(nullptr, 0);
        in.script_present = false;
        in.builtin_plan_present = false;
        const GateVerdict p = plan_gate(in);
        expect(!p.ok && p.reason == GateReason::PlanMissing, "absent plan must be refused");
        expect(std::string_view(p.path) == "backend.cve_2026_43499", "plan-missing path must be the backend");
        /* empty plan (extreme value): script present but naming nothing */
        PlanInput empty = base_input(nullptr, 0);
        expect(plan_gate(empty).reason == GateReason::PlanMissing, "empty plan must be refused");
        std::printf("case queue-removed: reason=%s path=%s | plan-missing: reason=%s path=%s\n",
                    gate_reason_name(q.reason), q.path, gate_reason_name(p.reason), p.path);
    }

    void case_multi_op_plan() {
        /* Two windows in one plan: legal (they are separate ops), index stable. */
        PlanOp ops[2] = {window_op(), window_op("window.pagecache", 0)};
        const PlanParam params[] = {{"burst_calls", 64U}};
        ops[1].params = params;
        ops[1].param_count = 1;
        ops[1].route_mask = route_bit("select_stack");
        const PlanInput in = base_input(ops, 2);
        const GateVerdict v = plan_gate(in);
        expect(v.ok, "two well-formed ops must pass");
        std::printf("case two ops: reason=%s path=%s\n", gate_reason_name(v.reason), v.path);
    }
}  // namespace

int main() {
    case_ok();
    case_param_at_bound_then_over();
    case_required_missing();
    case_undeclared_param();
    case_unknown_op();
    case_op_not_available();
    case_route_not_available();
    case_hash_mismatch();
    case_order_violation();
    case_landing_in_window();
    case_facts_missing();
    case_queue_and_plan_missing();
    case_multi_op_plan();
    if (failures == 0) {
        std::printf("plan-gate: PASS (12 rejections + ok + extremes)\n");
        return 0;
    }
    std::printf("plan-gate: FAIL (%d)\n", failures);
    return 1;
}
