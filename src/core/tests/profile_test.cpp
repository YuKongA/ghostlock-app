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
      !target_profile_supports_tcp_zerocopy(&profile) ||
      !target_profile_supports_select_stack(&profile) ||
      multicast.buffer_size != 128 || multicast.waiter_offset != 32 ||
      multicast.lock_slot_count != 4 || select.waiter_shift != 16 ||
      !select.compact_waiter || !tcp.compact_waiter ||
      !execution || execution->heap_prepare_max_attempts != 7 ||
      target_profile_mm_struct_sz(&profile, 0x500) != 0x580) {
    fputs("target profile snapshot/accessor test failed\n", stderr);
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

  /* PROFILE-SUGGEST-01: advisory execution fields fall back to the shipped
   * suggestions instead of being rejected. */
  struct execution_settings sparse = {};
  execution_settings_apply_suggestions(&sparse);
  const struct execution_settings suggested = execution_settings_suggested();
  if (sparse.recommended_main_cpu != 0 || sparse.recommended_consumer_cpu != 1 ||
      sparse.w1_attempts != suggested.w1_attempts ||
      sparse.tcp_arm_sequence != suggested.tcp_arm_sequence ||
      sparse.handoff_enforce_poll_interval_ms !=
              suggested.handoff_enforce_poll_interval_ms) {
    fputs("profile suggestion fallback test failed\n", stderr);
    return 1;
  }

  /* Out-of-range CPUs are repaired as a pair; arm_sequence clamps. */
  struct execution_settings invalid = {
      .recommended_main_cpu = 2048,
      .recommended_consumer_cpu = 2048,
      .tcp_attempts = 4,
      .tcp_arm_sequence = 16,
  };
  execution_settings_apply_suggestions(&invalid);
  if (invalid.recommended_main_cpu == invalid.recommended_consumer_cpu ||
      invalid.recommended_main_cpu >= 1024 ||
      invalid.recommended_consumer_cpu >= 1024 ||
      invalid.tcp_arm_sequence > invalid.tcp_attempts) {
    fputs("profile suggestion repair test failed\n", stderr);
    return 1;
  }
  puts("target profile snapshot/accessor test passed");
  return 0;
}
