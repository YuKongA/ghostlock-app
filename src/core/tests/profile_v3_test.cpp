/* Host test for the GLKv3 decode bridge (profile/glkv3_parse.cpp, GLKv3-4).
 *
 * Exercises the production v3 reader end to end: the canonical MessagePack
 * encoder feeds parse_v3, which decodes against the combined root + owner
 * declaration, maps the token selection to the binary ids and lands the values
 * on the frozen transport. Rejection vectors pin the fail-closed rules and the
 * route-less 43284 selection is covered. The v2 comparison vectors were removed
 * in S4 R2c with the v2 wire. */

#include "profile/glkv3_parse.hpp"
#include "profile_bind_compat.hpp"

#include "backend/cve_2026_43284_state.hpp"
#include "profile/glkv3.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ghostlock::tests::profile_bind::component_ids;
using ghostlock::profile::looks_like_glkv3;
using ghostlock::tests::profile_bind::parse_v3;
using ghostlock::profile::glkv3::Document;
using ghostlock::profile::glkv3::Entry;
using ghostlock::profile::glkv3::Section;
using ghostlock::profile::glkv3::Value;
using ghostlock::profile::glkv3::WireType;

namespace {
    Value u(uint64_t value) { Value out; out.type = WireType::UInt; out.uint_value = value; return out; }
    Value i(int64_t value) { Value out; out.type = WireType::Int; out.int_value = value; return out; }
    Value b(bool value) { Value out; out.type = WireType::Bool; out.bool_value = value; return out; }

    void put(Document &doc, std::string_view name, std::string_view key, Value value) {
        Section *section = doc.find_section(name);
        if (section == nullptr) section = &doc.append_section(name);
        section->entries.push_back(Entry{key, std::move(value)});
    }

    Document make_43499() {
        Document doc;
        doc.schema = 3;
        doc.has_release = true;
        doc.release = "6.6.77-v3-transport-test";
        doc.has_terminal = true;
        doc.terminal = "root_child";
        doc.has_backend = true;
        doc.backend = "cve_2026_43499";
        doc.has_route = true;
        doc.route = "multicast_waiter";
        /* HOCON refactor root scalars: root keys, not a "common" section. */
        doc.has_kernel_major = true;
        doc.kernel_major = 6;
        doc.has_kernel_minor = true;
        doc.kernel_minor = 15;
        doc.has_safe_mode = true;
        doc.safe_mode = true;
        put(doc, "backend.cve_2026_43499.abi.task_struct", "prio", u(132));
        put(doc, "backend.cve_2026_43499.abi.task_struct", "real_cred", u(0x12345678));
        put(doc, "backend.cve_2026_43499.cred", "copy_size", u(0x88));
        put(doc, "backend.cve_2026_43499.cred", "caps_value", u(0xffffffffffffffffULL));
        put(doc, "backend.cve_2026_43499.abi.offset", "init_task", u(0x20dc000));
        put(doc, "backend.cve_2026_43499.offset", "vr_sys_exit_tp", u(0x2a));
        put(doc, "backend.cve_2026_43499.kernel", "compact_waiter", b(true));
        put(doc, "backend.cve_2026_43499.kernel", "kernelsnitch_collisions", u(4));
        put(doc, "backend.cve_2026_43499.kernel", "mm_struct_sz", u(0x400));
        put(doc, "backend.cve_2026_43499.execution.stages", "w1_attempts", u(15));
        put(doc, "backend.cve_2026_43499.route.multicast_waiter", "waiter_off", i(96));
        put(doc, "backend.cve_2026_43499.route.multicast_waiter", "buffer_size", u(512));
        put(doc, "backend.cve_2026_43499", "steps", u(2));
        return doc;
    }

} // namespace

int main() {
    char release[64] = {0};
    ghostlock::profile::kernel_offsets v3parsed = {};
    component_ids v3ids = {};
    const std::string v3doc = ghostlock::profile::glkv3::encode(make_43499());
    assert(!v3doc.empty());
    assert(looks_like_glkv3(v3doc));
    assert(parse_v3(v3doc, &v3parsed, release, sizeof(release), &v3ids) == 0);
    assert(std::strcmp(release, "6.6.77-v3-transport-test") == 0);
    assert(std::strcmp(v3parsed.uname_r, release) == 0);
    assert(v3parsed.route == ghostlock::profile::kRouteMulticastWaiter);
    assert(v3parsed.meta.kernel_major == 6);
    assert(v3parsed.meta.kernel_minor == 15);
    assert(v3parsed.meta.safe_mode);
    /* vr_guard: the deleted common.vr_guard key has no writer left, so the
     * field stays false and platform/vivo stays provably inert (phase (b)). */
    assert(!v3parsed.misc.vr_guard);
    assert(v3parsed.misc.vr_tracepoint_funcs == 0);
    assert(v3parsed.task.prio == 132);
    assert(v3parsed.task.real_cred == 0x12345678);
    assert(v3parsed.credential.copy_size == 0x88);
    assert(v3parsed.credential.caps_value == 0xffffffffffffffffULL);
    assert(v3parsed.offsets.init_task == 0x20dc000);
    assert(v3parsed.misc.vr_sys_exit_tp == 0x2a);
    assert(v3parsed.misc.compact_waiter.value_or(false));
    assert(v3parsed.misc.kernelsnitch_collisions.value_or(0) == 4);
    assert(v3parsed.misc.mm_struct_sz.value_or(0) == 0x400);
    assert(v3parsed.execution.w1_attempts == 15);
    assert(v3parsed.geometry.mcast_waiter_off.value_or(0) == 96);
    assert(v3parsed.geometry.mcast_buffer_size.value_or(0) == 512);
    assert(v3ids.terminal == static_cast<uint16_t>(ghostlock::contract::TerminalKind::RootChild));
    assert(v3ids.backend == static_cast<uint16_t>(ghostlock::contract::BackendKind::Cve2026_43499));
    assert(v3ids.middleware == ghostlock::profile::kRouteMulticastWaiter);
    assert(v3ids.steps == 2);

    /* ---- Negative: the v2 magic root is not GLKv3 and is rejected. ---- */
    {
        char buf[64] = {0};
        ghostlock::profile::kernel_offsets parsed = {};
        const std::string v2doc("\x21\x07\x00\x0d\x02\x00", 6);
        assert(!looks_like_glkv3(v2doc));
        assert(parse_v3(v2doc, &parsed, buf, sizeof(buf)) == -1);
    }

    /* ---- Route-less 43284 v3 selection + private section. ---- */
    {
        Document doc;
        doc.schema = 3;
        doc.has_release = true;
        doc.release = "6.12.38-v3-43284";
        doc.has_terminal = true;
        doc.terminal = "umh_forward";
        doc.has_backend = true;
        doc.backend = "cve_2026_43284";
        put(doc, "backend.cve_2026_43284", "kmi", u(5150));
        put(doc, "backend.cve_2026_43284", "steps", u(3));
        const std::string encoded = ghostlock::profile::glkv3::encode(doc);
        ghostlock::profile::kernel_offsets parsed = {};
        component_ids ids = {};
        ghostlock::backend::Cve2026_43284Profile profile_43284{};
        char buf[64] = {0};
        assert(parse_v3(encoded, &parsed, buf, sizeof(buf), &ids, nullptr,
                        &profile_43284) == 0);
        assert(ids.backend == static_cast<uint16_t>(ghostlock::contract::BackendKind::Cve2026_43284));
        assert(ids.terminal == static_cast<uint16_t>(ghostlock::contract::TerminalKind::UmhForward));
        assert(ids.middleware == ghostlock::profile::kRouteNone);
        assert(parsed.route == ghostlock::profile::kRouteNone);
        assert(profile_43284.kmi.value_or(0) == 5150);
        assert(profile_43284.steps.value_or(0) == 3);
        assert(ids.steps == 3);
    }

    /* ---- Fail-closed rejection vectors. ---- */
    {
        char buf[64] = {0};
        ghostlock::profile::kernel_offsets parsed = {};

        assert(!looks_like_glkv3(std::string_view("\x01", 1)));
        assert(parse_v3(std::string_view("\x01", 1), &parsed, buf, sizeof(buf)) == -1);

        Document wrong = make_43499();
        wrong.schema = 2;
        assert(parse_v3(ghostlock::profile::glkv3::encode(wrong), &parsed, buf, sizeof(buf)) == -1);

        Document unknown_section = make_43499();
        put(unknown_section, "nope", "x", u(1));
        assert(parse_v3(ghostlock::profile::glkv3::encode(unknown_section), &parsed, buf, sizeof(buf)) == -1);

        Document unknown_key = make_43499();
        put(unknown_key, "backend.cve_2026_43499", "nope", u(1));
        assert(parse_v3(ghostlock::profile::glkv3::encode(unknown_key), &parsed, buf, sizeof(buf)) == -1);

        Document wrong_backend_section = make_43499();
        put(wrong_backend_section, "backend.cve_2026_43284", "kmi", u(5150));
        assert(parse_v3(ghostlock::profile::glkv3::encode(wrong_backend_section), &parsed, buf, sizeof(buf)) == -1);

        Document missing_route = make_43499();
        missing_route.has_route = false;
        missing_route.route = {};
        assert(parse_v3(ghostlock::profile::glkv3::encode(missing_route), &parsed, buf, sizeof(buf)) == -1);

        Document bad_route = make_43499();
        bad_route.route = "not_a_route";
        assert(parse_v3(ghostlock::profile::glkv3::encode(bad_route), &parsed, buf, sizeof(buf)) == -1);

        Document bad_terminal = make_43499();
        bad_terminal.terminal = "not_a_terminal";
        assert(parse_v3(ghostlock::profile::glkv3::encode(bad_terminal), &parsed, buf, sizeof(buf)) == -1);

        /* Trailing bytes after a valid document are rejected. */
        std::string trailing = v3doc;
        trailing.push_back(static_cast<char>(0));
        assert(parse_v3(trailing, &parsed, buf, sizeof(buf)) == -1);

        /* ---- HOCON refactor negatives: deleted owners and a mis-placed root
         * scalar are rejected whole (fail-closed, never a silent downgrade). */

        /* 1. Any "common.*" section: the owner is deleted -- kernel_major /
         *    safe_mode became root scalars and vr_guard disappeared. */
        Document common_section = make_43499();
        put(common_section, "common", "kernel_major", u(6));
        assert(parse_v3(ghostlock::profile::glkv3::encode(common_section), &parsed,
                        buf, sizeof(buf)) == -1);

        /* 2. Any "countermeasure.*" section: deleting common.vr_guard and
         *    countermeasure.vivo_vr_guard.tracepoint_funcs emptied the owner,
         *    so it left the whitelist too. */
        Document countermeasure_section = make_43499();
        put(countermeasure_section, "countermeasure.vivo_vr_guard",
            "tracepoint_funcs", u(0x20));
        assert(parse_v3(ghostlock::profile::glkv3::encode(countermeasure_section),
                        &parsed, buf, sizeof(buf)) == -1);

        /* 3. kernel_major inside a section: a root scalar lives at the document
         *    root only, so the owner bind rejects the mis-placed key. */
        Document scalar_in_section = make_43499();
        put(scalar_in_section, "backend.cve_2026_43499", "kernel_major", u(6));
        assert(parse_v3(ghostlock::profile::glkv3::encode(scalar_in_section), &parsed,
                        buf, sizeof(buf)) == -1);

        /* 4. The old "platform.abi.*" path: the platform owner is deleted and
         *    its keys live at backend.cve_2026_43499.abi.* now, so the stale
         *    spelling is rejected instead of being silently ignored. */
        Document old_platform_path = make_43499();
        put(old_platform_path, "platform.abi.task_struct", "prio", u(132));
        assert(parse_v3(ghostlock::profile::glkv3::encode(old_platform_path), &parsed,
                        buf, sizeof(buf)) == -1);

        /* 5. The old flattened 43284 execution key: the five knobs live under
         *    backend.cve_2026_43284.execution.* now (the wire carries the
         *    execution. level too), so the flat spelling is rejected instead of
         *    being silently ignored. */
        Document flat_execution_key;
        flat_execution_key.schema = 3;
        flat_execution_key.has_release = true;
        flat_execution_key.release = "6.12.38-neg-flat-execution";
        flat_execution_key.has_terminal = true;
        flat_execution_key.terminal = "umh_forward";
        flat_execution_key.has_backend = true;
        flat_execution_key.backend = "cve_2026_43284";
        put(flat_execution_key, "backend.cve_2026_43284", "late_load_args", u(2));
        assert(parse_v3(ghostlock::profile::glkv3::encode(flat_execution_key),
                        &parsed, buf, sizeof(buf)) == -1);
    }

    std::puts("profile_v3_test: OK");
    return 0;
}
