/** @file tests/route_config_test.cpp
 *  纯构建的判据：值齐 ⇒ 逐字段对得上；任一位缺 ⇒ **具名拒绝**（能失败的那一面）。
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "backend/cve_2026_43499/steps/route_config.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::steps::build_route_config;
    using ghostlock::backend::cve_2026_43499::steps::RouteConfigOutcome;
    using ghostlock::backend::cve_2026_43499::steps::RouteConfigValues;

    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) {
            std::printf("route config: FAIL %s\n", what);
            ++g_failures;
        }
    }

    /** 一份"调用方都说了"的值（对应用户文档里的那一组）。 */
    RouteConfigValues full_values() {
        RouteConfigValues values{};
        values.waiter_offset = 0x60U;
        values.task_offset = 0x30U;
        values.lock_offset = 0x38U;
        values.buffer_size = 0x108U;
        values.max_attempts = 8U;
        values.arm_hold = 64U;
        values.arm_sequence = 2U;
        values.park_ms = 1000U;
        values.settle_us = 50000U;
        values.consumer_delay_us = 0U;
        values.burst_calls = 4U;
        values.max_calls = 4U;
        values.fallback_lock_us = 50000U;
        values.nice_modulus = 19U;
        values.policy = 0U;
        values.target = 0xffff800000018800ULL;
        values.value = 0x4141414141414141ULL;
        values.fake_task = 0x1000ULL;
        values.fake_lock = 0x2000ULL;
        return values;
    }

    void a_full_set_maps_field_by_field() {
        const RouteConfigOutcome out = build_route_config(full_values());
        expect(out.refusal == nullptr, "a full set is not refused");
        expect(out.config.stamp.waiter_offset == 0x60U, "waiter offset");
        expect(out.config.stamp.task_offset == 0x30U, "task offset");
        expect(out.config.stamp.lock_offset == 0x38U, "lock offset");
        expect(out.config.stamp.buffer_size == 0x108U, "buffer size");
        expect(out.config.limits.max_attempts == 8, "attempts");
        expect(out.config.limits.arm_hold == 64, "arm hold");
        expect(out.config.limits.arm_sequence == 2, "arm sequence");
        expect(out.config.park_ms == 1000, "park");
        expect(out.config.settle_us == 50000, "settle");
        expect(out.config.consumer_delay_us == 0, "delay");
        expect(out.config.burst_calls == 4, "burst");
        expect(out.config.max_calls == 4, "max calls");
        expect(out.config.fallback_lock_us == 50000, "fallback lock");
        expect(out.config.nice_modulus == 19, "nice modulus");
        expect(out.config.target == 0xffff800000018800ULL, "target");
        expect(out.config.value == 0x4141414141414141ULL, "value");
        expect(out.config.fake_task == 0x1000U, "fake task");
        expect(out.config.fake_lock == 0x2000U, "fake lock");
    }

    /** 每一位都必须**能失败**：把它清零，拒绝名必须点到它。 */
    void every_required_zero_is_named() {
        struct Field final {
            const char *name;
            std::uint64_t RouteConfigValues::*member;
        };
        const Field fields[] = {
            {"buffer_size", &RouteConfigValues::buffer_size},
            {"max_attempts", &RouteConfigValues::max_attempts},
            {"arm_sequence", &RouteConfigValues::arm_sequence},
            {"arm_hold", &RouteConfigValues::arm_hold},
            {"park_ms", &RouteConfigValues::park_ms},
            {"settle_us", &RouteConfigValues::settle_us},
            {"burst_calls", &RouteConfigValues::burst_calls},
            {"max_calls", &RouteConfigValues::max_calls},
            {"fallback_lock_us", &RouteConfigValues::fallback_lock_us},
            {"nice_modulus", &RouteConfigValues::nice_modulus},
            {"target", &RouteConfigValues::target},
            /* `value` 不在此表（目标「取得 uid0」✓）：Zero 模式的写值**合法地就是 0** ✗，
             *  归档成功链的 w3 正是零写（真机三步读数 layout=0x0 ✓）⇒ 它必须被接受 ✓。 */
            {"fake_task", &RouteConfigValues::fake_task},
            {"fake_lock", &RouteConfigValues::fake_lock},
        };
        for (const Field &field : fields) {
            RouteConfigValues values = full_values();
            values.*(field.member) = 0U;
            const RouteConfigOutcome out = build_route_config(values);
            const char *expected = nullptr;
            char wanted[96] = {};
            std::snprintf(wanted, sizeof(wanted), "route-config-missing:%s", field.name);
            expected = wanted;
            expect(out.refusal != nullptr, field.name);
            expect(out.refusal != nullptr && std::strcmp(out.refusal, expected) == 0, field.name);
        }
    }

    /** 上界：装不进 int32 的数要被点名，而不是截断。 */
    void too_big_is_named() {
        RouteConfigValues values = full_values();
        values.park_ms = 0x80000000ULL;
        const RouteConfigOutcome out = build_route_config(values);
        expect(out.refusal != nullptr && std::strcmp(out.refusal, "route-config-too-big:park_ms") == 0,
               "too big is named");
    }
    /** 零值合法（目标「取得 uid0」✓）：`Zero` 的写值就是 0 ✗（归档 w3 即零写 ✓），不得与"缺失"混同 ✓。 */
    void zero_value_is_accepted() {
        RouteConfigValues values = full_values();
        values.value = 0U;
        const RouteConfigOutcome out = build_route_config(values);
        expect(out.refusal == nullptr, "zero value is accepted");
        expect(out.config.value == 0U, "zero value reaches the config");
    }
}  // namespace

int main() {
    std::printf("route config: the pure build");
    a_full_set_maps_field_by_field();
    every_required_zero_is_named();
    too_big_is_named();
    zero_value_is_accepted();
    if (g_failures != 0U) {
        std::printf("route config: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("route config: PASS (a full set maps field by field, every required zero is named, too-big is named)\n");
    return 0;
}
