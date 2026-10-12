/** @file tests/prepare_mapping_test.cpp
 *  准备映射的证据：15 个输入各有 schema 键与常量拒绝名；值齐则填进单元结构体，缺一即具名。
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "backend/cve_2026_43499/steps/prepare_mapping.hpp"

using namespace ghostlock;

namespace {
    namespace steps = backend::cve_2026_43499::steps;

    std::size_t g_failures = 0U;

    void expect(bool ok, const char *what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++g_failures; }
    }

    steps::PrepareValues full_values() noexcept {
        steps::PrepareValues values{};
        values.snitch_collisions = 4U;
        values.snitch_threads = 1U;
        values.futex_table_size = 256U;
        values.mm_struct_sz = 1024U;
        values.region_threads = 4U;
        values.snitch_round_wait_us = 2000U;
        values.snitch_wake_rounds = 200U;
        values.prepare_max_attempts = 4U;
        values.prepare_timeout_ms = 240000U;
        values.kernelsnitch_timeout_ms = 60000U;
        values.setup_settle_us = 50000U;
        values.state_poll_interval_us = 1000U;
        values.route_wait_ms = 1000U;
        values.route_done_timeout_ms = 300000U;
        values.waiter_offset = 40U;
        values.task_offset = 32U;
        values.lock_offset = 24U;
        values.buffer_size = 65536U;
        /* 区域/几何那一组（键已进 schema；这里的数字是测试自己的值）。 */
        values.page_size = 4096U;
        values.header_bytes = 64U;
        values.region_size = 4194304U;
        values.object_size = 256U;
        values.total_pokes = 64U;
        values.pre_groups = 1U;
        values.spray_groups = 2U;
        values.post_groups = 1U;
        values.leak_groups = 1U;
        values.word_bytes = 8U;
        values.handle_bytes = 8U;
        values.coarse_stride = 4096U;
        values.slab_stride = 1024U;
        values.mm_objs_per_slab = 2U;
        values.shaped = 1U;
        values.socket_buffer = 262144U;
        values.send_size = 1024U;
        return values;
    }

    void the_table_names_every_input() {
        expect(steps::kPrepareInputKeyCount == 37U, "thirty-seven inputs are declared");
        std::size_t declared = 0U;
        std::size_t undeclared = 0U;
        for (std::size_t i = 0U; i < steps::kPrepareInputKeyCount; ++i) {
            const steps::PrepareInputKey &row = steps::kPrepareInputKeys[i];
            expect(row.input != nullptr && row.refusal != nullptr, "every row names itself and its refusal");
            if (row.wire_key == nullptr) {
                /* 表里的「已声明缺口」：schema 还没有这个键 ⇒ 文档供不上。 */
                ++undeclared;
                expect(std::strncmp(row.refusal, "prepare-key-undeclared:", 23U) == 0,
                       "an undeclared input says so");
            } else {
                ++declared;
                expect(std::strncmp(row.wire_key, "backend.cve_2026_43499.", 22U) == 0,
                       "a declared key lives under this backend");
                expect(std::strncmp(row.refusal, "prepare-value-missing:", 22U) == 0,
                       "a declared input refuses with the missing-value name");
            }
        }
        expect(declared == 37U && undeclared == 0U, "all thirty-seven inputs are declared now");
    }

    void complete_values_fill_the_units() {
        steps::PrepareMapped mapped{};
        const char *refusal = nullptr;
        expect(steps::map_prepare_inputs(full_values(), mapped, &refusal), "a complete document maps");
        expect(mapped.snitch.collisions == 4U && mapped.snitch.thread_count == 1U,
               "the snitch inputs come from the wire");
        expect(mapped.geometry.waiter_offset == 40U && mapped.geometry.buffer_size == 65536U,
               "the geometry offset came across");
        expect(mapped.prepare_timeout_ms == 240000U && mapped.route_wait_ms == 1000U,
               "the budgets came across");
    }

    void the_region_inputs_come_from_the_same_values() {
        steps::PrepareMapped mapped{};
        const char *refusal = nullptr;
        steps::PrepareValues values = full_values();
        values.word_bytes = 8U;
        values.handle_bytes = 8U;
        expect(steps::map_prepare_inputs(values, mapped, &refusal), "the base mapping still maps");
        steps::RegionMapped region{};
        expect(steps::map_region_inputs(values, region, &refusal), "the region fills from the same values");
        expect(region.inputs.page_size == 4096U && region.inputs.header_bytes == 64U,
               "the page and header widths came across");
        expect(region.inputs.collisions == 4U && region.inputs.thread_count == 4U,
               "so did the search sizes");
        expect(region.inputs.word_bytes == 8U && region.inputs.handle_bytes == 8U,
               "and the two widths");
        expect(region.strides.coarse == 4096U && region.strides.slab == 1024U && region.strides.candidate == 1024U,
               "the strides came from the same values");
        expect(region.patience.wake_rounds == 200U && region.patience.round_timeout_us == 2000,
               "the patience came from the same keys");
        values.word_bytes = 0U;
        expect(!steps::map_region_inputs(values, region, &refusal) &&
                       std::strcmp(refusal, "prepare-value-missing:word_bytes") == 0,
               "a missing width is named");
    }
    void the_reclaim_counts_come_from_the_same_values() {
        steps::PrepareValues values = full_values();
        steps::ReclaimMapped reclaim{};
        const char *refusal = nullptr;
        expect(steps::map_reclaim_inputs(values, reclaim, &refusal), "the reclaim fills");
        expect(reclaim.counts.pre == 1U && reclaim.counts.spray == 2U && reclaim.counts.post == 1U,
               "the group counts came across");
        expect(reclaim.counts.mm_objs_per_slab == 2U && reclaim.counts.shaped, "and the slab rule");
        expect(reclaim.shaping.socket_buffer == 262144 && reclaim.shaping.send_size == 1024U,
               "and the shaping sizes");
        values.mm_objs_per_slab = 0U;
        expect(!steps::map_reclaim_inputs(values, reclaim, &refusal) &&
                       std::strcmp(refusal, "prepare-value-missing:mm_objs_per_slab") == 0,
               "a missing slab rule is named");
    }
    void the_segments_map_in_order_and_name_the_first_missing() {
        steps::PrepareValues values = full_values();
        steps::AllMapped all{};
        const char *refusal = nullptr;
        expect(steps::map_all_prepare(values, all, &refusal), "a complete document maps all three");
        expect(all.base.prepare_timeout_ms == 240000U && all.region.inputs.page_size == 4096U &&
                       all.reclaim.counts.spray == 2U,
               "each segment got its own part");
        /* 第一段缺 ⇒ 它自己的名字；第二段缺 ⇒ 它的；第三段缺 ⇒ 它的。 */
        values.snitch_threads = 0U;
        expect(!steps::map_all_prepare(values, all, &refusal) &&
                       std::strcmp(refusal, "prepare-value-missing:snitch_threads") == 0,
               "the base segment speaks first");
        values = full_values();
        values.page_size = 0U;
        expect(!steps::map_all_prepare(values, all, &refusal) &&
                       std::strcmp(refusal, "prepare-value-missing:page_size") == 0,
               "then the region segment");
        values = full_values();
        values.send_size = 0U;
        expect(!steps::map_all_prepare(values, all, &refusal) &&
                       std::strcmp(refusal, "prepare-value-missing:send_size") == 0,
               "then the reclaim segment");
    }
    void a_missing_value_is_named() {
        steps::PrepareMapped mapped{};
        const char *refusal = nullptr;
        steps::PrepareValues values = full_values();
        values.route_wait_ms = 0U;   /* 文档没带这个键 */
        expect(!steps::map_prepare_inputs(values, mapped, &refusal), "a zero value refuses");
        expect(refusal != nullptr && std::strcmp(refusal, "prepare-value-missing:route_wait_ms") == 0,
               "and the refusal names that input");
        values = full_values();
        values.page_size = 0U;
        values.buffer_size = 65536U;   /* 只让这一个为零 */
        expect(!steps::map_prepare_inputs(values, mapped, &refusal) &&
                       std::strcmp(refusal, "prepare-value-missing:page_size") == 0,
               "the region group refuses too when it is missing");
        values = full_values();
        values.buffer_size = 0U;
        expect(!steps::map_prepare_inputs(values, mapped, &refusal) &&
                       std::strcmp(refusal, "prepare-value-missing:buffer_size") == 0,
               "a second zero names its own input");
    }
}  // namespace

int main() {
    std::printf("prepare mapping: the table");
    the_table_names_every_input();
    std::printf("prepare mapping: the region group");
    the_region_inputs_come_from_the_same_values();
    std::printf("prepare mapping: the reclaim group");
    the_reclaim_counts_come_from_the_same_values();
    std::printf("prepare mapping: all three segments");
    the_segments_map_in_order_and_name_the_first_missing();
    std::printf("prepare mapping: filling the units");
    complete_values_fill_the_units();
    a_missing_value_is_named();
    if (g_failures != 0U) {
        std::printf("prepare mapping: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("prepare mapping: PASS (fifteen inputs, one table, named misses)\n");
    return 0;
}
