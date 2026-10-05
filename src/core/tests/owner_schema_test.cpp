/* Host test for the cve_2026_43499 owner Schema (ADR-0003 / A2-3c-2).
 *
 * Covers: every declared (section, key) binds in Production (strict) and the
 * key set has no owner collision; the backend-private steps key lands on
 * View::steps; Production rejects an unknown section and an unknown key; and a
 * full serialize -> parse -> serialize round trip reproduces the original bytes
 * for each route, proving the View lands on the frozen kernel_offsets layout. */

#include "backend/cve_2026_43499/schema.hpp"
#include "platform/abi.hpp"
#include "profile/binary.h"
#include "profile_bind_compat.hpp"
#include "profile/document.hpp"
#include "profile/schema.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>

using ghostlock::backend::Cve2026_43499Schema;
using ghostlock::backend::Cve2026_43499View;
using ghostlock::profile::BindCode;
using ghostlock::profile::DecodeMode;
using ghostlock::profile::Document;
using ghostlock::profile::kernel_offsets;
using ghostlock::profile::Section;

namespace {
    void add(Document &doc, std::string_view section, std::string_view key,
             uint64_t raw) {
        Section *found = doc.find_section(section);
        if (!found) found = &doc.append_section(section);
        found->add(key, raw);
    }

    /* One Document carrying every declared key with raw = 1. */
    Document full_document() {
        Document doc;
        doc.release = "6.6.77-owner-schema";
        for (const auto &field : Cve2026_43499Schema::kFields) {
            add(doc, field.section, field.key, 1ULL);
        }
        return doc;
    }

    /* Distinctive, in-range values for every owned field, including all three
     * route sections (the serializer emits only the active one). */
    void fill_all(kernel_offsets &v) {
        v.meta.kernel_major = 6;
        v.meta.fallback_route = ghostlock::profile::kRouteSelectStack;
        v.meta.safe_mode = true;
        v.task.prio = 101;
        v.task.normal_prio = 102;
        v.task.sched_task_group = 103;
        v.task.pi_lock = 104;
        v.task.pi_waiters = 105;
        v.task.pi_top_task = 106;
        v.task.pi_blocked_on = 107;
        v.task.pid = 108;
        v.task.tgid = 109;
        v.task.atomic_flags = 110;
        v.task.real_cred = 111;
        v.task.cred = 112;
        v.task.comm = 113;
        v.task.tasks = 114;
        v.task.seccomp = 115;
        v.credential.copy_size = 200;
        v.credential.usage_offset = 201;
        v.credential.usage_value = 202;
        v.credential.caps_offset = 203;
        v.credential.caps_count = 204;
        v.credential.caps_value = 0x123456789abcdef0ULL;
        v.credential.ref_count = 205;
        v.credential.ref0_offset = 206;
        v.credential.ref1_offset = 207;
        v.credential.ref2_offset = 208;
        v.credential.ref3_offset = 209;
        v.credential.ref0_image = 0x1111111111111111ULL;
        v.credential.ref1_image = 0x2222222222222222ULL;
        v.credential.ref2_image = 0x3333333333333333ULL;
        v.credential.ref3_image = 0x4444444444444444ULL;
        v.offsets.init_task = 0x1000;
        v.offsets.init_cred = 0x2000;
        v.offsets.empty_zero_page = 0x3000;
        v.offsets.root_task_group = 0x4000;
        v.offsets.selinux_enforcing = 0x5000;
        v.offsets.selinux_blob_sizes = 0x6000;
        v.offsets.security_hook_heads = 0x7000;
        v.offsets.slide_nfulnl_logger = 0x8000;
        v.offsets.slide_loggers_0_1 = 0x9000;
        v.offsets.slide_boot_id = 0xa000;
        v.misc.kernel_phys_load = 0xb000;
        v.misc.kernel_phys_offset = 0xc000;
        v.misc.compact_waiter = true;
        v.misc.vr_guard = true;
        v.misc.vr_tracepoint_funcs = 0x20;
        v.misc.kernelsnitch_collisions = 7;
        v.misc.mm_struct_sz = 0x400;
        v.misc.vr_sys_exit_tp = 0x2a;
        v.geometry.pselect_waiter_shift = -2;
        v.geometry.mcast_waiter_off = 96;
        v.geometry.mcast_buffer_size = 512;
        v.geometry.mcast_task_offset = 0x30;
        v.geometry.mcast_lock_offset = 0x40;
        v.geometry.tcp_payload_delta = -0x80;
        v.geometry.tcp_chunk_bias = 0x2000;
        v.geometry.tcp_fake_task_off = 0x888;
        v.geometry.tcp_cred_copy_off = 0x999;
        v.execution.recommended_main_cpu = 0;
        v.execution.recommended_consumer_cpu = 1;
        v.execution.heap_prepare_max_attempts = 10;
        v.execution.heap_prepare_timeout_ms = 11;
        v.execution.heap_kernelsnitch_timeout_ms = 12;
        v.execution.race_route_wait_ms = 13;
        v.execution.race_route_done_timeout_ms = 300000;
        v.execution.race_setup_settle_us = 14;
        v.execution.race_state_poll_interval_us = 15;
        v.execution.w1_attempts = 16;
        v.execution.w1_settle_us = 17;
        v.execution.w1_scratch_repair_attempts = 18;
        v.execution.w2_attempts = 19;
        v.execution.w2_settle_us = 20;
        v.execution.w3_chain_rounds = 21;
        v.execution.w3_attempts = 22;
        v.execution.w3_settle_us = 23;
        v.execution.tcp_attempts = 24;
        v.execution.tcp_arm_sequence = 25;
        v.execution.tcp_post_receive_hold_iterations = 26;
        v.execution.select_enter_delay_us = 27;
        v.execution.select_timeout_us = 28;
        v.execution.select_consumer_max_calls = 29;
        v.execution.select_consumer_burst_calls = 30;
        v.execution.handoff_pre_dispatch_settle_ms = 31;
        v.execution.handoff_module_poll_attempts = 32;
        v.execution.handoff_module_poll_interval_ms = 33;
        v.execution.handoff_enforce_poll_attempts = 34;
        v.execution.handoff_enforce_poll_interval_ms = 35;
        v.mcast_attempts = 128;
        v.mcast_arm_sequence = 16;
        v.mcast_arm_hold = 20000;
    }

    int32_t round_trip(uint8_t route, kernel_offsets &values, char *first,
                       size_t capacity, int32_t &first_size) {
        char second[8192];
        char release[64] = {0};
        values.route = route;
        first_size = ghostlock::binary_profile::serialize(&values, first, capacity);
        if (first_size <= 0) return -1;
        kernel_offsets parsed = {};
        const int32_t rc = ghostlock::binary_profile::parse(
                std::string_view(first, static_cast<size_t>(first_size)),
                &parsed, release, sizeof(release));
        if (rc != 0) return rc;
        const int32_t second_size = ghostlock::binary_profile::serialize(
                &parsed, second, sizeof(second));
        if (second_size != first_size) return -2;
        return std::memcmp(first, second, static_cast<size_t>(first_size)) == 0
                       ? 0
                       : -3;
    }
} // namespace

int main() {
    constexpr size_t kFieldCount = std::size(Cve2026_43499Schema::kFields);

    /* ---- Ownership: no two specs share one (section, key). ---- */
    for (size_t i = 0; i < kFieldCount; i++) {
        for (size_t j = i + 1; j < kFieldCount; j++) {
            const bool same = Cve2026_43499Schema::kFields[i].section ==
                                      Cve2026_43499Schema::kFields[j].section &&
                              Cve2026_43499Schema::kFields[i].key ==
                                      Cve2026_43499Schema::kFields[j].key;
            assert(!same);
        }
    }

    /* ---- Production accepts every declared key; steps lands on the View. ---- */
    {
        Document doc = full_document();
        Cve2026_43499View view{};
        const auto status = ghostlock::profile::bind<Cve2026_43499Schema>(
                doc, view, DecodeMode::Production);
        assert(status.ok());
        assert(view.steps == 1);
        assert(view.values.execution.w1_attempts == 1);
        assert(view.values.geometry.tcp_payload_delta.value_or(99) == 1);
        assert(view.values.misc.compact_waiter.value_or(false));
    }

    /* ---- Production rejects an unknown section; Tooling tolerates it. ---- */
    {
        Document doc = full_document();
        doc.append_section("not_a_section").add("x", 1ULL);
        Cve2026_43499View view{};
        const auto blocked = ghostlock::profile::bind<Cve2026_43499Schema>(
                doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::UnknownSection);
        assert(blocked.section == "not_a_section");
        assert(ghostlock::profile::bind<Cve2026_43499Schema>(
                       doc, view, DecodeMode::Tooling)
                       .ok());
    }

    /* ---- Production rejects an unknown key in an owned section. ---- */
    {
        Document doc = full_document();
        doc.find_section("meta")->add("bogus", 1ULL);
        Cve2026_43499View view{};
        const auto blocked = ghostlock::profile::bind<Cve2026_43499Schema>(
                doc, view, DecodeMode::Production);
        assert(blocked.code == BindCode::UnknownKey);
        assert(blocked.section == "meta");
        assert(blocked.key == "bogus");
    }

    /* ---- Full transport round trip, one pass per route. ---- */
    {
        kernel_offsets values = {};
        values.uname_r = "6.6.77-owner-round-trip";
        values.meta.kernel_major = 6;
        fill_all(values);
        char first[8192];
        int32_t size = 0;
        for (uint8_t route : {ghostlock::profile::kRouteTcpZerocopy,
                              ghostlock::profile::kRouteSelectStack,
                              ghostlock::profile::kRouteMulticastWaiter}) {
            assert(round_trip(route, values, first, sizeof(first), size) == 0);
        }
    }

    /* ---- A2-4-3: platform + backend union binds with one ownership
     * picture, and the platform View merges into the frozen transport. ---- */
    {
        Document doc;
        doc.release = "6.6.77-union";
        for (const auto &field : ghostlock::platform::abi::Schema::kFields) {
            add(doc, field.section, field.key, 7ULL);
        }
        for (const auto &field : Cve2026_43499Schema::kFields) {
            add(doc, field.section, field.key, 3ULL);
        }
        /* No (section, key) is declared by both owners. */
        for (const auto &p : ghostlock::platform::abi::Schema::kFields) {
            for (const auto &b : Cve2026_43499Schema::kFields) {
                assert(!(p.section == b.section && p.key == b.key));
            }
        }
        ghostlock::platform::abi::View abi_view{};
        Cve2026_43499View view{};
        const auto status = ghostlock::profile::bind_all<
                ghostlock::platform::abi::Schema, Cve2026_43499Schema>(
                doc, DecodeMode::Production, abi_view, view);
        assert(status.ok());
        assert(abi_view.task.prio == 7);
        assert(abi_view.offset.init_task == 7);
        assert(view.values.credential.copy_size == 3);
        ghostlock::platform::abi::apply_to(abi_view, view.values);
        assert(view.values.task.prio == 7);
        assert(view.values.credential.usage_offset == 7);
        assert(view.values.offsets.init_task == 7);
        assert(view.values.misc.kernel_phys_load.value_or(0) == 7);
        assert(view.values.credential.copy_size == 3);

        /* Union validation still rejects an unknown key. */
        Document bad = doc;
        bad.find_section("meta")->add("bogus", 1ULL);
        ghostlock::platform::abi::View abi2{};
        Cve2026_43499View view2{};
        const auto blocked = ghostlock::profile::bind_all<
                ghostlock::platform::abi::Schema, Cve2026_43499Schema>(
                bad, DecodeMode::Production, abi2, view2);
        assert(blocked.code == BindCode::UnknownKey);
    }

    std::printf("owner_schema_test: ok\n");
    return 0;
}
