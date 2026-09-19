#include "profile.h"

#include <stdio.h>

int main(void) {
  struct kernel_offsets decoded = {
      .kernel_major = 5,
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
  TargetProfile profile = target_profile_snapshot(&decoded);
  decoded.kernel_major = 6;
  decoded.execution.heap_prepare_max_attempts = 99;

  MulticastWaiterLayout multicast =
      target_profile_multicast_waiter_layout(&profile);
  SelectStackLayout select = target_profile_select_stack_layout(&profile);
  TcpZerocopyLayout tcp = target_profile_tcp_zerocopy_layout(&profile);
  const struct execution_settings *execution =
      target_profile_execution(&profile);

  if (!target_profile_supports_multicast_waiter(&profile) ||
      target_profile_supports_tcp_zerocopy(&profile) ||
      target_profile_supports_select_stack(&profile) ||
      multicast.buffer_size != 128 || multicast.waiter_offset != 32 ||
      multicast.lock_slot_count != 4 || select.waiter_shift != 16 ||
      !select.compact_waiter || !tcp.compact_waiter ||
      !execution || execution->heap_prepare_max_attempts != 7 ||
      target_profile_mm_struct_sz(&profile, 0x500) != 0x580) {
    fputs("target profile snapshot/accessor test failed\n", stderr);
    return 1;
  }

  /* An explicit route field overrides the legacy geometry inference. */
  decoded.route = kRouteTcpZerocopy;
  TargetProfile explicit_tcp = target_profile_snapshot(&decoded);
  decoded.route = kRouteSelectStack;
  TargetProfile explicit_select = target_profile_snapshot(&decoded);
  if (!target_profile_supports_tcp_zerocopy(&explicit_tcp) ||
      target_profile_supports_multicast_waiter(&explicit_tcp) ||
      !target_profile_supports_select_stack(&explicit_select) ||
      target_profile_supports_tcp_zerocopy(&explicit_select)) {
    fputs("explicit route selection test failed\n", stderr);
    return 1;
  }

  /* A zero profile field and an unloaded profile both use the fallback. */
  decoded.mm_struct_sz = 0;
  TargetProfile zero_stride = target_profile_snapshot(&decoded);
  TargetProfile unloaded{};
  if (target_profile_mm_struct_sz(&zero_stride, 0x500) != 0x500 ||
      target_profile_mm_struct_sz(&unloaded, 0x500) != 0x500 ||
      target_profile_mm_struct_sz(nullptr, 0x500) != 0x500) {
    fputs("target profile mm_struct stride fallback test failed\n", stderr);
    return 1;
  }

  puts("target profile snapshot/accessor test passed");
  return 0;
}
