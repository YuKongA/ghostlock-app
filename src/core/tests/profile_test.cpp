#include "profile/model.h"

#include <cstdio>

using namespace lsprivilege;

int32_t main(void) {
    profile::kernel_offsets decoded = {
        .route = lsprivilege::profile::kRouteMulticastWaiter,
        .meta = {.kernel_major = 5},
        .misc = {.compact_waiter = 1, .mm_struct_sz = 0x580},
        .geometry = {
            .pselect_waiter_shift = 16,
            .mcast_waiter_off = 32,
            .mcast_buffer_size = 128,
            .mcast_task_offset = 40,
            .mcast_lock_offset = 48,
        },
        .execution = {
            .recommended_main_cpu = 2,
            .recommended_consumer_cpu = 3,
            .heap_prepare_max_attempts = 7,
        },
    };
    lsprivilege::profile::TargetProfile profile = lsprivilege::profile::TargetProfile::from(&decoded);
    decoded.meta.kernel_major = 6;
    decoded.execution.heap_prepare_max_attempts = 99;

    lsprivilege::profile::MulticastWaiterLayout multicast =
            profile.multicast_layout();
    lsprivilege::profile::SelectStackLayout select = profile.select_stack_layout();
    lsprivilege::profile::TcpZerocopyLayout tcp = profile.tcp_zerocopy_layout();
    const profile::execution_settings *execution =
            profile.execution();

    if (!profile.supports(lsprivilege::profile::RouteKind::MulticastWaiter) ||
        profile.supports(lsprivilege::profile::RouteKind::TcpZerocopy) ||
        profile.supports(lsprivilege::profile::RouteKind::SelectStack) ||
        multicast.buffer_size.value_or(0) != 128 ||
        multicast.waiter_offset.value_or(0) != 32 ||
        select.waiter_shift.value_or(0) != 16 ||
        select.compact_waiter.value_or(0) == 0 ||
        tcp.compact_waiter.value_or(0) == 0 ||
        !execution || execution->heap_prepare_max_attempts != 7 ||
        profile.mm_struct_stride(0x500) != 0x580) {
        fputs("target profile snapshot/accessor test failed\n", stderr);
        return 1;
    }

    /* The declared route decides, and kRouteAuto selects no chain at all. */
    decoded.route = lsprivilege::profile::kRouteTcpZerocopy;
    lsprivilege::profile::TargetProfile explicit_tcp = lsprivilege::profile::TargetProfile::from(&decoded);
    decoded.route = lsprivilege::profile::kRouteSelectStack;
    lsprivilege::profile::TargetProfile explicit_select = lsprivilege::profile::TargetProfile::from(&decoded);
    decoded.route = lsprivilege::profile::kRouteAuto;
    lsprivilege::profile::TargetProfile unresolved = lsprivilege::profile::TargetProfile::from(&decoded);
    if (!explicit_tcp.supports(lsprivilege::profile::RouteKind::TcpZerocopy) ||
        explicit_tcp.supports(lsprivilege::profile::RouteKind::MulticastWaiter) ||
        !explicit_select.supports(lsprivilege::profile::RouteKind::SelectStack) ||
        explicit_select.supports(lsprivilege::profile::RouteKind::TcpZerocopy) ||
        unresolved.supports(lsprivilege::profile::RouteKind::MulticastWaiter) ||
        unresolved.supports(lsprivilege::profile::RouteKind::TcpZerocopy) ||
        unresolved.supports(lsprivilege::profile::RouteKind::SelectStack)) {
        fputs("explicit route selection test failed\n", stderr);
        return 1;
    }

    /* A zero profile field and an unloaded profile both use the fallback. */
    decoded.misc.mm_struct_sz = 0;
    lsprivilege::profile::TargetProfile zero_stride = lsprivilege::profile::TargetProfile::from(&decoded);
    lsprivilege::profile::TargetProfile unloaded{};
    if (zero_stride.mm_struct_stride(0x500) != 0x500 ||
        unloaded.mm_struct_stride(0x500) != 0x500 ||
        profile::TargetProfile{}.mm_struct_stride(0x500) != 0x500) {
        fputs("target profile mm_struct stride fallback test failed\n", stderr);
        return 1;
    }

    puts("target profile snapshot/accessor test passed");
    return 0;
}
