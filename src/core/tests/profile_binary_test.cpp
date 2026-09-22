/* Host test for the binary profile transport shared with Kotlin (GLK1 v4:
 * common slots + per-route section). */

#include "profile/binary.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string_view>

using namespace ghostlock;

static int32_t round_trip(const profile::kernel_offsets &values, profile::kernel_offsets *parsed,
                          char *release, size_t release_cap) {
    char buffer[4096];
    const int32_t size = binary_profile::serialize(&values, buffer, sizeof(buffer));
    if (size <= 0) return -1;
    return binary_profile::parse(std::string_view(buffer, static_cast<size_t>(size)),
                                 parsed, release, release_cap);
}

int32_t main(void) {
    char release[64] = {0};
    profile::kernel_offsets parsed = {};

    /* Common slots are route-independent and always present. */
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
    /* multicast route section */
    values.mcast_waiter_off = 96;
    values.mcast_buffer_size = 512;
    values.off_mcast_fake_bss = 0x2000;
    values.multicast_resident = 1;
    values.execution.multicast_ready_timeout_ms = 1234;
    values.execution.multicast_post_requeue_settle_us = 300;

    assert(round_trip(values, &parsed, release, sizeof(release)) == 0);
    assert(strcmp(release, values.uname_r) == 0);
    assert(strcmp(parsed.uname_r, values.uname_r) == 0);
    assert(parsed.kernel_major == 6 && parsed.recommend_shizuku == 1);
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

    /* TCP route carries only its own section. */
    values.route = ghostlock::profile::kRouteTcpZerocopy;
    values.execution.tcp_attempts = 2000;
    values.execution.tcp_arm_sequence = 7;
    assert(round_trip(values, &parsed, release, sizeof(release)) == 0);
    assert(parsed.execution.tcp_attempts == 2000);
    assert(parsed.execution.tcp_arm_sequence == 7);

    /* Select route carries its own section. */
    values.route = ghostlock::profile::kRouteSelectStack;
    values.pselect_waiter_shift = -2;
    values.execution.select_enter_delay_us = 50000;
    assert(round_trip(values, &parsed, release, sizeof(release)) == 0);
    assert(parsed.pselect_waiter_shift == -2);
    assert(parsed.execution.select_enter_delay_us == 50000);

    /* Rejected vectors: bad magic, truncated body, short release buffer. */
    char buffer[4096];
    const int32_t size = binary_profile::serialize(&values, buffer, sizeof(buffer));
    assert(size > 0);
    char broken[4096];
    memcpy(broken, buffer, static_cast<size_t>(size));
    broken[0] = 'X';
    assert(binary_profile::parse(
               std::string_view(broken, static_cast<size_t>(size)),
               &parsed, release, sizeof(release)) == -1);
    assert(binary_profile::parse(
               std::string_view(buffer, static_cast<size_t>(size) - 1),
               &parsed, release, sizeof(release)) == -1);
    char tiny_release[4] = {0};
    assert(binary_profile::parse(
               std::string_view(buffer, static_cast<size_t>(size)),
               &parsed, tiny_release, sizeof(tiny_release)) == -1);

    /* A profile without a declared route never serializes. */
    values.route = ghostlock::profile::kRouteAuto;
    assert(binary_profile::serialize(&values, buffer, sizeof(buffer)) == -1);

    puts("profile_binary_test: ok");
    return 0;
}
