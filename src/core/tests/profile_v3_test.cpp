/* Host test for the GLKv3 decode bridge (profile/glkv3_parse.cpp, GLKv3-4).
 *
 * Exercises the production v3 reader end to end: the canonical MessagePack
 * encoder feeds parse_v3, which decodes against the combined root + owner
 * declaration, maps the token selection to the binary ids and lands the values
 * on the frozen transport. A v2 document with the same logical content is
 * parsed through the v2 reader and the two results are compared field by field,
 * so the two wires cannot drift. Rejection vectors pin the fail-closed rules
 * and the route-less 43284 selection is covered. */

#include "profile/glkv3_parse.hpp"

#include "backend/cve_2026_43284_state.hpp"
#include "profile/binary.h"
#include "profile/glkv3.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using ghostlock::binary_profile::component_ids;
using ghostlock::binary_profile::looks_like_glkv3;
using ghostlock::binary_profile::parse_v3;
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
        put(doc, "meta", "kernel_major", u(6));
        put(doc, "meta", "fallback_route", u(2));
        put(doc, "meta", "safe_mode", b(true));
        put(doc, "meta", "vr_guard", b(true));
        put(doc, "task_struct", "prio", u(132));
        put(doc, "task_struct", "real_cred", u(0x12345678));
        put(doc, "cred", "copy_size", u(0x88));
        put(doc, "cred", "caps_value", u(0xffffffffffffffffULL));
        put(doc, "offset", "init_task", u(0x20dc000));
        put(doc, "offset", "vr_sys_exit_tp", u(0x2a));
        put(doc, "kernel", "compact_waiter", b(true));
        put(doc, "kernel", "kernelsnitch_collisions", u(4));
        put(doc, "kernel", "mm_struct_sz", u(0x400));
        put(doc, "execution.stages", "w1_attempts", u(15));
        put(doc, "route.multicast_waiter", "waiter_off", i(96));
        put(doc, "route.multicast_waiter", "buffer_size", u(512));
        put(doc, "backend.cve_2026_43499", "steps", u(2));
        return doc;
    }

    namespace v2 {
        void put_u16(std::string &out, uint16_t value) {
            out.push_back(static_cast<char>(value & 0xff));
            out.push_back(static_cast<char>((value >> 8) & 0xff));
        }
        void put_u32(std::string &out, uint32_t value) {
            for (int k = 0; k < 4; k++) out.push_back(static_cast<char>((value >> (8 * k)) & 0xff));
        }
        void put_u64(std::string &out, uint64_t value) {
            for (int k = 0; k < 8; k++) out.push_back(static_cast<char>((value >> (8 * k)) & 0xff));
        }
        struct VEntry { std::string key; int64_t value; };
        struct VSection { std::string name; std::vector<VEntry> entries; };

        std::string build_43499(const std::string &release,
                                const std::vector<VSection> &sections) {
            std::string out;
            put_u32(out, ghostlock::binary_profile::kMagic);
            put_u16(out, ghostlock::binary_profile::kVersion);
            put_u16(out, ghostlock::binary_profile::kTerminalRootChild);
            put_u16(out, ghostlock::binary_profile::kBackendCve202643499);
            put_u16(out, ghostlock::profile::kRouteMulticastWaiter);
            put_u16(out, static_cast<uint16_t>(release.size()));
            put_u16(out, 0);
            out += release;
            put_u16(out, static_cast<uint16_t>(sections.size()));
            for (const VSection &section : sections) {
                out.push_back(static_cast<char>(section.name.size()));
                out += section.name;
                put_u32(out, static_cast<uint32_t>(section.entries.size()));
                for (const VEntry &entry : section.entries) {
                    out.push_back(static_cast<char>(entry.key.size()));
                    out += entry.key;
                    put_u64(out, static_cast<uint64_t>(entry.value));
                }
            }
            return out;
        }
    } // namespace v2
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
    assert(v3parsed.meta.fallback_route == ghostlock::profile::kRouteSelectStack);
    assert(v3parsed.meta.safe_mode);
    assert(v3parsed.misc.vr_guard);
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
    assert(v3ids.terminal == ghostlock::binary_profile::kTerminalRootChild);
    assert(v3ids.backend == ghostlock::binary_profile::kBackendCve202643499);
    assert(v3ids.middleware == ghostlock::profile::kRouteMulticastWaiter);
    assert(v3ids.steps == 2);

    /* ---- v2 and v3 with the same logical content agree field by field. ---- */
    {
        const std::string v2doc = v2::build_43499("6.6.77-v3-transport-test", {
            {"meta", {{"kernel_major", 6}, {"fallback_route", 2}, {"safe_mode", 1}, {"vr_guard", 1}}},
            {"task_struct", {{"prio", 132}, {"real_cred", 0x12345678}}},
            {"cred", {{"copy_size", 0x88}, {"caps_value", -1}}},
            {"offset", {{"init_task", 0x20dc000}, {"vr_sys_exit_tp", 0x2a}}},
            {"kernel", {{"compact_waiter", 1}, {"kernelsnitch_collisions", 4}, {"mm_struct_sz", 0x400}}},
            {"execution.stages", {{"w1_attempts", 15}}},
            {"route.multicast_waiter", {{"waiter_off", 96}, {"buffer_size", 512}}},
            {"backend.cve_2026_43499", {{"steps", 2}}},
        });
        char v2release[64] = {0};
        ghostlock::profile::kernel_offsets v2parsed = {};
        component_ids v2ids = {};
        assert(!looks_like_glkv3(v2doc));
        assert(ghostlock::binary_profile::parse(v2doc, &v2parsed, v2release,
                                                sizeof(v2release), &v2ids) == 0);
        assert(std::strcmp(v2release, release) == 0);
        assert(v2parsed.route == v3parsed.route);
        assert(v2parsed.meta.kernel_major == v3parsed.meta.kernel_major);
        assert(v2parsed.meta.fallback_route == v3parsed.meta.fallback_route);
        assert(v2parsed.meta.safe_mode == v3parsed.meta.safe_mode);
        assert(v2parsed.misc.vr_guard == v3parsed.misc.vr_guard);
        assert(v2parsed.task.prio == v3parsed.task.prio);
        assert(v2parsed.task.real_cred == v3parsed.task.real_cred);
        assert(v2parsed.credential.copy_size == v3parsed.credential.copy_size);
        assert(v2parsed.credential.caps_value == v3parsed.credential.caps_value);
        assert(v2parsed.offsets.init_task == v3parsed.offsets.init_task);
        assert(v2parsed.misc.vr_sys_exit_tp == v3parsed.misc.vr_sys_exit_tp);
        assert(v2parsed.misc.compact_waiter == v3parsed.misc.compact_waiter);
        assert(v2parsed.misc.kernelsnitch_collisions == v3parsed.misc.kernelsnitch_collisions);
        assert(v2parsed.misc.mm_struct_sz == v3parsed.misc.mm_struct_sz);
        assert(v2parsed.execution.w1_attempts == v3parsed.execution.w1_attempts);
        assert(v2parsed.geometry.mcast_waiter_off == v3parsed.geometry.mcast_waiter_off);
        assert(v2parsed.geometry.mcast_buffer_size == v3parsed.geometry.mcast_buffer_size);
        assert(v2ids.terminal == v3ids.terminal);
        assert(v2ids.backend == v3ids.backend);
        assert(v2ids.middleware == v3ids.middleware);
        assert(v2ids.steps == v3ids.steps);
    }

    /* ---- Route-less 43284 v3 selection + private section. ---- */
    {
        Document doc;
        doc.schema = 3;
        doc.has_release = true;
        doc.release = "6.12.38-v3-43284";
        doc.has_terminal = true;
        doc.terminal = "root_child";
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
        assert(ids.backend == ghostlock::binary_profile::kBackendCve202643284);
        assert(ids.terminal == ghostlock::binary_profile::kTerminalRootChild);
        assert(ids.middleware == ghostlock::profile::kRouteAuto);
        assert(parsed.route == ghostlock::profile::kRouteAuto);
        assert(profile_43284.kmi.value_or(0) == 5150);
        assert(profile_43284.steps.value_or(0) == 3);
        assert(ids.steps == 3);
    }

    /* ---- Fail-closed rejection vectors. ---- */
    {
        char buf[64] = {0};
        ghostlock::profile::kernel_offsets parsed = {};
        component_ids ids = {};

        assert(!looks_like_glkv3(std::string_view("\x01", 1)));
        assert(parse_v3(std::string_view("\x01", 1), &parsed, buf, sizeof(buf)) == -1);

        Document wrong = make_43499();
        wrong.schema = 2;
        assert(parse_v3(ghostlock::profile::glkv3::encode(wrong), &parsed, buf, sizeof(buf)) == -1);

        Document unknown_section = make_43499();
        put(unknown_section, "nope", "x", u(1));
        assert(parse_v3(ghostlock::profile::glkv3::encode(unknown_section), &parsed, buf, sizeof(buf)) == -1);

        Document unknown_key = make_43499();
        put(unknown_key, "meta", "nope", u(1));
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
    }

    std::puts("profile_v3_test: OK");
    return 0;
}
