/* Host test for the binary profile transport shared with Kotlin. */

#include "profile_binary.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace ghostlock;

int main(void) {
  struct kernel_offsets values = {};
  values.uname_r = "6.6.77-transport-test";
  values.kernel_major = 6;
  values.recommend_shizuku = 1;
  values.route = kRouteTcpZerocopy;
  values.fallback_route = kRouteSelectStack;
  values.task_prio = 132;
  values.task_real_cred = 0x12345678;
  values.cred_copy_size = 0x88;
  values.cred_caps_value = 0xffffffffffffffffULL;
  values.off_init_task = 0x20dc000;
  values.off_slide_boot_id = 0x2336600;
  values.mcast_waiter_off = 96;
  values.pselect_waiter_shift = -2;
  values.compact_waiter = 1;
  values.kernelsnitch_collisions = 4;
  values.mm_struct_sz = 0x400;
  values.execution.w1_attempts = 15;
  values.execution.tcp_attempts = 2000;
  values.execution.handoff_enforce_poll_interval_ms = 100;

  char buffer[4096];
  const int size = binary_profile::serialize(&values, buffer, sizeof(buffer));
  assert(size > 0);

  struct kernel_offsets parsed = {};
  char release[64] = {0};
  assert(binary_profile::parse(
             std::string_view(buffer, static_cast<size_t>(size)),
             &parsed, release, sizeof(release)) == 0);

  assert(strcmp(release, values.uname_r) == 0);
  assert(strcmp(parsed.uname_r, values.uname_r) == 0);
  assert(parsed.kernel_major == 6 && parsed.recommend_shizuku == 1);
  assert(parsed.route == kRouteTcpZerocopy);
  assert(parsed.fallback_route == kRouteSelectStack);
  assert(parsed.task_prio == 132);
  assert(parsed.task_real_cred == 0x12345678);
  assert(parsed.cred_copy_size == 0x88);
  assert(parsed.cred_caps_value == 0xffffffffffffffffULL);
  assert(parsed.off_init_task == 0x20dc000);
  assert(parsed.off_slide_boot_id == 0x2336600);
  assert(parsed.mcast_waiter_off == 96);
  assert(parsed.pselect_waiter_shift == -2);
  assert(parsed.compact_waiter == 1);
  assert(parsed.kernelsnitch_collisions == 4);
  assert(parsed.mm_struct_sz == 0x400);
  assert(parsed.execution.w1_attempts == 15);
  assert(parsed.execution.tcp_attempts == 2000);
  assert(parsed.execution.handoff_enforce_poll_interval_ms == 100);

  /* Rejected vectors: bad magic, truncated body, short release buffer. */
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
  values.route = kRouteAuto;
  assert(binary_profile::serialize(&values, buffer, sizeof(buffer)) == -1);

  puts("profile_binary_test: ok");
  return 0;
}
