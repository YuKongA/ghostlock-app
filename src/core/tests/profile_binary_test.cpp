/* Host test for the binary profile transport shared with Kotlin (v2: common
 * slots + per-route section).
 *
 * Black-box: round trips and rejected vectors through the public parse/serialize
 * API.
 * White-box: a local builder pins the exact header bytes, the 68 common slot
 * indices, and the per-entry route-section decoding (unknown/duplicate keys,
 * truncation, other-route keys). */

#include "profile/binary.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace ghostlock;

namespace {
    constexpr size_t kCommonCount = 68;

    void put_u16(std::string &out, uint16_t value) {
        out.push_back(static_cast<char>(value & 0xff));
        out.push_back(static_cast<char>((value >> 8) & 0xff));
    }

    void put_u32(std::string &out, uint32_t value) {
        for (int i = 0; i < 4; i++) out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }

    void put_u64(std::string &out, uint64_t value) {
        for (int i = 0; i < 8; i++) out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
    }

    struct Entry {
        std::string key;
        int64_t value;
    };

    /* Builds one v2 document byte by byte so tests can inject anything. */
    std::string build_doc(uint8_t route, uint8_t major, uint8_t recommend, uint8_t fallback,
                          const std::string &release,
                          const std::vector<uint64_t> &common,
                          const std::vector<Entry> &entries) {
        std::string out;
        put_u32(out, binary_profile::kMagic);
        put_u16(out, binary_profile::kVersionV2);
        out.push_back(static_cast<char>(route));
        out.push_back(static_cast<char>(major));
        out.push_back(static_cast<char>(recommend));
        out.push_back(static_cast<char>(fallback));
        put_u16(out, static_cast<uint16_t>(release.size()));
        out += release;
        assert(common.size() == kCommonCount);
        for (uint64_t value : common) put_u64(out, value);
        out.push_back(static_cast<char>(entries.size()));
        for (const Entry &entry : entries) {
            out.push_back(static_cast<char>(entry.key.size()));
            out += entry.key;
            put_u64(out, static_cast<uint64_t>(entry.value));
        }
        return out;
    }

    int32_t parse_doc(const std::string &doc, profile::kernel_offsets *out, char *release,
                      size_t release_cap) {
        return binary_profile::parse(std::string_view(doc.data(), doc.size()), out, release,
                                     release_cap);
    }

    int32_t round_trip(const profile::kernel_offsets &values, profile::kernel_offsets *parsed,
                       char *release, size_t release_cap) {
        char buffer[4096];
        const int32_t size = binary_profile::serialize(&values, buffer, sizeof(buffer));
        if (size <= 0) return -1;
        return binary_profile::parse(std::string_view(buffer, static_cast<size_t>(size)), parsed,
                                     release, release_cap);
    }
} // namespace

int32_t main(void) {
    char release[64] = {0};
    profile::kernel_offsets parsed = {};

    /* ---- Black box: round trips through the public API. ---- */
    profile::kernel_offsets values = {};
    values.uname_r = "6.6.77-transport-test";
    values.kernel_major = 6;
    values.recommend_shizuku = 1;
    values.route = ghostlock::profile::kRouteMulticastWaiter;
    values.fallback_route = ghostlock::profile::kRouteSelectStack;
    values.task_prio = 132;
    values.task_real_cred = 0x12345678;
    values.cred_copy_size = 0x88;
    values.cred_caps_value = 0xffffffffffffffffULL;
    values.off_init_task = 0x20dc000;
    values.off_slide_boot_id = 0x2336600;
    values.compact_waiter = 1;
    values.kernelsnitch_collisions = 4;
    values.mm_struct_sz = 0x400;
    values.execution.w1_attempts = 15;
    values.execution.handoff_enforce_poll_interval_ms = 100;
    values.mcast_waiter_off = 96;
    values.mcast_buffer_size = 512;
    values.off_mcast_fake_bss = 0x2000;
    values.multicast_resident = 1;
    values.execution.multicast_ready_timeout_ms = 1234;
    values.execution.multicast_post_requeue_settle_us = 300;

    assert(round_trip(values, &parsed, release, sizeof(release)) == 0);
    assert(strcmp(release, values.uname_r) == 0);
    assert(strcmp(parsed.uname_r, values.uname_r) == 0);
    /* v3 drops recommend_shizuku from the transport; it stays App-only. */
    assert(parsed.kernel_major == 6 && parsed.recommend_shizuku == 0);
    assert(parsed.route == ghostlock::profile::kRouteMulticastWaiter);
    assert(parsed.fallback_route == ghostlock::profile::kRouteSelectStack);
    assert(parsed.task_prio == 132);
    assert(parsed.task_real_cred == 0x12345678);
    assert(parsed.cred_copy_size == 0x88);
    assert(parsed.cred_caps_value == 0xffffffffffffffffULL);
    assert(parsed.off_init_task == 0x20dc000);
    assert(parsed.off_slide_boot_id == 0x2336600);
    assert(parsed.compact_waiter == 1);
    assert(parsed.kernelsnitch_collisions == 4);
    assert(parsed.mm_struct_sz == 0x400);
    assert(parsed.execution.w1_attempts == 15);
    assert(parsed.execution.handoff_enforce_poll_interval_ms == 100);
    assert(parsed.mcast_waiter_off == 96);
    assert(parsed.mcast_buffer_size == 512);
    assert(parsed.off_mcast_fake_bss == 0x2000);
    assert(parsed.multicast_resident == 1);
    assert(parsed.execution.multicast_ready_timeout_ms == 1234);
    assert(parsed.execution.multicast_post_requeue_settle_us == 300);

    values.route = ghostlock::profile::kRouteTcpZerocopy;
    values.execution.tcp_attempts = 2000;
    values.execution.tcp_arm_sequence = 7;
    assert(round_trip(values, &parsed, release, sizeof(release)) == 0);
    assert(parsed.execution.tcp_attempts == 2000);
    assert(parsed.execution.tcp_arm_sequence == 7);

    values.route = ghostlock::profile::kRouteSelectStack;
    values.pselect_waiter_shift = -2;
    values.execution.select_enter_delay_us = 50000;
    assert(round_trip(values, &parsed, release, sizeof(release)) == 0);
    assert(parsed.pselect_waiter_shift == -2);
    assert(parsed.execution.select_enter_delay_us == 50000);

    /* ---- White box: header bytes. ---- */
    {
        char buffer[4096];
        const int32_t size = binary_profile::serialize(&values, buffer, sizeof(buffer));
        assert(size > 0);
        const auto *bytes = reinterpret_cast<const unsigned char *>(buffer);
        const uint32_t magic = static_cast<uint32_t>(bytes[0]) |
                               (static_cast<uint32_t>(bytes[1]) << 8) |
                               (static_cast<uint32_t>(bytes[2]) << 16) |
                               (static_cast<uint32_t>(bytes[3]) << 24);
        const uint16_t version = static_cast<uint16_t>(bytes[4] | (bytes[5] << 8));
        const uint16_t middleware = static_cast<uint16_t>(bytes[10] | (bytes[11] << 8));
        assert(magic == 0x0D000721u && binary_profile::kMagic == 0x0D000721u);
        assert(version == 3u && binary_profile::kVersion == 3u);
        assert(middleware == ghostlock::profile::kRouteSelectStack);
        assert(bytes[12] == 6);
        assert(bytes[13] == ghostlock::profile::kRouteSelectStack);
        const size_t middleware_bytes =
            2 + (1 + std::strlen("pselect_waiter_shift") + 8) +
            (1 + std::strlen("select_enter_delay_us") + 8) +
            (1 + std::strlen("select_timeout_us") + 8);
        const size_t options_bytes =
            2 + (1 + std::strlen("safe_mode") + 8) +
            (1 + std::strlen("selected_cpus.main") + 8) +
            (1 + std::strlen("selected_cpus.consumer") + 8);
        assert(static_cast<size_t>(size) ==
               16 + std::strlen(values.uname_r) + kCommonCount * 8 +
               middleware_bytes + options_bytes);
    }

    /* ---- White box: every common slot index. ---- */
    {
        std::vector<uint64_t> common(kCommonCount, 0);
        common[0] = 0x11;   /* task_prio */
        common[14] = 0x22;  /* task_seccomp */
        common[15] = 0x33;  /* cred_copy_size */
        common[29] = 0x44;  /* cred_ref3_image */
        common[30] = 0x55;  /* off_init_task */
        common[39] = 0x66;  /* off_slide_boot_id */
        common[40] = 0x77;  /* kernel_phys_load */
        common[41] = 1;     /* compact_waiter */
        common[42] = 4;     /* kernelsnitch_collisions */
        common[43] = 0x400; /* mm_struct_sz */
        common[44] = 0;     /* recommended_main_cpu */
        common[45] = 1;     /* recommended_consumer_cpu */
        common[65] = 1;     /* select_consumer_max_calls (common, all routes) */
        common[66] = 1;     /* select_consumer_burst_calls */
        common[67] = 1;     /* safe_mode */
        const std::string doc = build_doc(ghostlock::profile::kRouteSelectStack, 6, 0, 0,
                                          "slot-test", common,
                                          {{"select_enter_delay_us", 50000}});
        assert(parse_doc(doc, &parsed, release, sizeof(release)) == 0);
        assert(parsed.task_prio == 0x11);
        assert(parsed.task_seccomp == 0x22);
        assert(parsed.cred_copy_size == 0x33);
        assert(parsed.cred_ref3_image == 0x44);
        assert(parsed.off_init_task == 0x55);
        assert(parsed.off_slide_boot_id == 0x66);
        assert(parsed.kernel_phys_load == 0x77);
        assert(parsed.compact_waiter == 1);
        assert(parsed.kernelsnitch_collisions == 4);
        assert(parsed.mm_struct_sz == 0x400);
        assert(parsed.execution.recommended_main_cpu == 0);
        assert(parsed.execution.recommended_consumer_cpu == 1);
        assert(parsed.execution.select_consumer_max_calls == 1);
        assert(parsed.execution.select_consumer_burst_calls == 1);
        assert(parsed.safe_mode == 1);
        assert(parsed.execution.select_enter_delay_us == 50000);
    }

    /* ---- White box: route-section entry decoding. ---- */
    {
        std::vector<uint64_t> common(kCommonCount, 0);

        /* Unknown keys are ignored; known keys keep their value. */
        std::string doc = build_doc(ghostlock::profile::kRouteTcpZerocopy, 6, 0, 0, "unknown-key",
                                    common,
                                    {{"not_a_field", 123}, {"tcp_attempts", 2000}});
        assert(parse_doc(doc, &parsed, release, sizeof(release)) == 0);
        assert(parsed.execution.tcp_attempts == 2000);

        /* Keys of another route are ignored by this route's table. */
        doc = build_doc(ghostlock::profile::kRouteTcpZerocopy, 6, 0, 0, "other-route", common,
                        {{"pselect_waiter_shift", 9}});
        assert(parse_doc(doc, &parsed, release, sizeof(release)) == 0);
        assert(parsed.pselect_waiter_shift == 0);

        /* A duplicated key applies last-wins. */
        doc = build_doc(ghostlock::profile::kRouteTcpZerocopy, 6, 0, 0, "duplicate", common,
                        {{"tcp_attempts", 1}, {"tcp_attempts", 2}});
        assert(parse_doc(doc, &parsed, release, sizeof(release)) == 0);
        assert(parsed.execution.tcp_attempts == 2);

        /* Signed values sign-extend exactly like the transport member. */
        doc = build_doc(ghostlock::profile::kRouteSelectStack, 6, 0, 0, "signed", common,
                        {{"pselect_waiter_shift", -2}});
        assert(parse_doc(doc, &parsed, release, sizeof(release)) == 0);
        assert(parsed.pselect_waiter_shift == -2);

        /* An empty route section is valid (all route fields stay zero). */
        doc = build_doc(ghostlock::profile::kRouteMulticastWaiter, 5, 1, 0, "empty-section",
                        common, {});
        assert(parse_doc(doc, &parsed, release, sizeof(release)) == 0);
        assert(parsed.mcast_waiter_off == 0);

        /* Zero-length release is accepted by the parser. */
        doc = build_doc(ghostlock::profile::kRouteSelectStack, 6, 0, 0, "", common, {});
        assert(parse_doc(doc, &parsed, release, sizeof(release)) == 0);
        assert(release[0] == '\0');

        /* Truncations: missing count byte, missing entry body, short value. */
        doc = build_doc(ghostlock::profile::kRouteTcpZerocopy, 6, 0, 0, "trunc", common,
                        {{"tcp_attempts", 5}});
        /* One past the count byte (entries start here). */
        const size_t count_at = doc.size() - (1 + std::strlen("tcp_attempts") + 8);
        assert(parse_doc(doc.substr(0, count_at - 1), &parsed, release, sizeof(release)) == -1);
        assert(parse_doc(doc.substr(0, count_at + 1), &parsed, release, sizeof(release)) == -1);
        assert(parse_doc(doc.substr(0, count_at + 1 + 1 + 4), &parsed, release,
                         sizeof(release)) == -1);

        /* Count larger than the entries actually present. */
        std::string lying = doc;
        lying[count_at - 1] = 3;
        assert(parse_doc(lying, &parsed, release, sizeof(release)) == -1);
    }

    /* ---- Black box: rejected headers and routes. ---- */
    {
        char buffer[4096];
        const int32_t size = binary_profile::serialize(&values, buffer, sizeof(buffer));
        assert(size > 0);
        char broken[4096];
        memcpy(broken, buffer, static_cast<size_t>(size));
        broken[0] = 'X';
        assert(binary_profile::parse(std::string_view(broken, static_cast<size_t>(size)), &parsed,
                                     release, sizeof(release)) == -1);
        assert(binary_profile::parse(std::string_view(buffer, static_cast<size_t>(size) - 1),
                                     &parsed, release, sizeof(release)) == -1);
        char tiny_release[4] = {0};
        assert(binary_profile::parse(std::string_view(buffer, static_cast<size_t>(size)), &parsed,
                                     tiny_release, sizeof(tiny_release)) == -1);

        std::vector<uint64_t> common(kCommonCount, 0);
        /* build_doc emits the v2 layout, so only version 2 of this vector is
         * valid; v3 acceptance is covered by the serialize round trips above. */
        for (int version : {1, 2, 3, 4, 5}) {
            std::string doc = build_doc(ghostlock::profile::kRouteSelectStack, 6, 0, 0, "v", common, {});
            doc[4] = static_cast<char>(version & 0xff);
            doc[5] = static_cast<char>(version >> 8);
            const int32_t rc = parse_doc(doc, &parsed, release, sizeof(release));
            assert((version == 2) == (rc == 0));
        }
        for (int route : {0, 4, 99}) {
            const std::string doc =
                build_doc(static_cast<uint8_t>(route), 6, 0, 0, "r", common, {});
            assert(parse_doc(doc, &parsed, release, sizeof(release)) == -1);
        }

        /* Batch 3.1: v3 component ids are validated and the u16 middleware id
         * is not truncated. */
        {
            profile::kernel_offsets v3 = {};
            v3.uname_r = "component-ids";
            v3.route = ghostlock::profile::kRouteSelectStack;
            char v3buf[4096];
            const int32_t v3size = binary_profile::serialize(&v3, v3buf, sizeof(v3buf));
            assert(v3size > 0);
            auto parse_v3 = [&](int32_t frontend, int32_t backend, int32_t middleware) {
                char buf[4096];
                memcpy(buf, v3buf, static_cast<size_t>(v3size));
                buf[6] = static_cast<char>(frontend & 0xff);
                buf[7] = static_cast<char>((frontend >> 8) & 0xff);
                buf[8] = static_cast<char>(backend & 0xff);
                buf[9] = static_cast<char>((backend >> 8) & 0xff);
                buf[10] = static_cast<char>(middleware & 0xff);
                buf[11] = static_cast<char>((middleware >> 8) & 0xff);
                return binary_profile::parse(std::string_view(buf, static_cast<size_t>(v3size)),
                                             &parsed, release, sizeof(release));
            };
            /* A successful decode must report the exact ids, not just reject
             * bad ones: this pins the wire -> selection contract. */
            binary_profile::component_ids ids{};
            char idbuf[4096];
            memcpy(idbuf, v3buf, static_cast<size_t>(v3size));
            assert(binary_profile::parse(std::string_view(idbuf, static_cast<size_t>(v3size)),
                                         &parsed, release, sizeof(release), &ids) == 0);
            assert(ids.frontend == binary_profile::kFrontendRootChild);
            assert(ids.backend == binary_profile::kBackendCve202643499);
            assert(ids.middleware == ghostlock::profile::kRouteSelectStack);

            assert(parse_v3(binary_profile::kFrontendRootChild,
                            binary_profile::kBackendCve202643499,
                            ghostlock::profile::kRouteSelectStack) == 0);
            assert(parse_v3(9, binary_profile::kBackendCve202643499,
                            ghostlock::profile::kRouteSelectStack) == -1);
            assert(parse_v3(binary_profile::kFrontendRootChild, 9,
                            ghostlock::profile::kRouteSelectStack) == -1);
            assert(parse_v3(binary_profile::kFrontendRootChild,
                            binary_profile::kBackendCve202643499, 99) == -1);
        }

        /* A profile without a declared route never serializes. */
        values.route = ghostlock::profile::kRouteAuto;
        assert(binary_profile::serialize(&values, buffer, sizeof(buffer)) == -1);
    }

    puts("profile_binary_test: ok");
    return 0;
}
