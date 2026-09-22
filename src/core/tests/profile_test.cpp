#include "profile.h"

#include <cstdio>

int main(void) {
    struct kernel_offsets decoded = {
        .kernel_major = 5,
        .route = kRouteMulticastWaiter,
        .pselect_waiter_shift = 16,
        .mcast_waiter_off = 32,
        .mcast_buffer_size = 128,
        .mcast_task_offset = 40,
        .mcast_lock_offset = 48,
        .mcast_fake_lock_offset = 0x100,
        .mcast_fake_task_offset = 0x200,
        .mcast_lock_slots_offset = 0x300,
        .mcast_lock_slot_count = 4,
        .mcast_lock_slot_stride = 64,
        .off_mcast_fake_bss = 0x123400,
        .compact_waiter = 1,
        .mm_struct_sz = 0x580,
        .execution = {
            .recommended_main_cpu = 2,
            .recommended_consumer_cpu = 3,
            .heap_prepare_max_attempts = 7,
        },
    };
    TargetProfile profile = TargetProfile::from(&decoded);
    decoded.kernel_major = 6;
    decoded.execution.heap_prepare_max_attempts = 99;

    MulticastWaiterLayout multicast =
            profile.multicast_layout();
    SelectStackLayout select = profile.select_stack_layout();
    TcpZerocopyLayout tcp = profile.tcp_zerocopy_layout();
    const struct execution_settings *execution =
            profile.execution();

    if (!profile.supports(::RouteKind::MulticastWaiter) ||
        profile.supports(::RouteKind::TcpZerocopy) ||
        profile.supports(::RouteKind::SelectStack) ||
        multicast.buffer_size != 128 || multicast.waiter_offset != 32 ||
        multicast.lock_slot_count != 4 || select.waiter_shift != 16 ||
        !select.compact_waiter || !tcp.compact_waiter ||
        !execution || execution->heap_prepare_max_attempts != 7 ||
        profile.mm_struct_stride(0x500) != 0x580) {
        fputs("target profile snapshot/accessor test failed\n", stderr);
        return 1;
    }

    /* The declared route decides, and kRouteAuto selects no chain at all. */
    decoded.route = kRouteTcpZerocopy;
    TargetProfile explicit_tcp = TargetProfile::from(&decoded);
    decoded.route = kRouteSelectStack;
    TargetProfile explicit_select = TargetProfile::from(&decoded);
    decoded.route = kRouteAuto;
    TargetProfile unresolved = TargetProfile::from(&decoded);
    if (!explicit_tcp.supports(::RouteKind::TcpZerocopy) ||
        explicit_tcp.supports(::RouteKind::MulticastWaiter) ||
        !explicit_select.supports(::RouteKind::SelectStack) ||
        explicit_select.supports(::RouteKind::TcpZerocopy) ||
        unresolved.supports(::RouteKind::MulticastWaiter) ||
        unresolved.supports(::RouteKind::TcpZerocopy) ||
        unresolved.supports(::RouteKind::SelectStack)) {
        fputs("explicit route selection test failed\n", stderr);
        return 1;
    }

    /* A zero profile field and an unloaded profile both use the fallback. */
    decoded.mm_struct_sz = 0;
    TargetProfile zero_stride = TargetProfile::from(&decoded);
    TargetProfile unloaded{};
    if (zero_stride.mm_struct_stride(0x500) != 0x500 ||
        unloaded.mm_struct_stride(0x500) != 0x500 ||
        TargetProfile{}.mm_struct_stride(0x500) != 0x500) {
        fputs("target profile mm_struct stride fallback test failed\n", stderr);
        return 1;
    }

    puts("target profile snapshot/accessor test passed");
    return 0;
}
