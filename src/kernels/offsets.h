#ifndef OFFSETS_H
#define OFFSETS_H

#include <stdint.h>

struct kernel_offsets {
  const char *uname_r;
  /* Bootloader-selected physical load address; 0 uses target.h. */
  uint64_t kernel_phys_load;
  /* pselect fd_set waiter word shift; 0 uses target.h default. */
  int pselect_waiter_shift;
  uint64_t off_init_task, off_init_cred;
  uint64_t off_root_task_group, off_selinux_enforcing;
  uint64_t off_selinux_blob_sizes, off_security_hook_heads;
  uint64_t off_slide_nfulnl_logger, off_slide_loggers_0_1, off_slide_boot_id;

  /* Per-kernel struct offsets; 0 uses target.h defaults. */
  uint32_t task_prio, task_normal_prio, task_sched_task_group;
  uint32_t task_pi_lock, task_pi_waiters, task_pi_top_task, task_pi_blocked_on;
  uint32_t task_pid, task_tgid, task_atomic_flags;
  uint32_t task_real_cred, task_cred, task_comm, task_tasks, task_seccomp;

  /* rt_mutex_waiter layout: 0 = 6.6 rb_node, 1 = 6.1 compact tree_entry */
  uint8_t compact_waiter;
  /* mm_struct SLUB stride; 0 uses target.h default (6.6 GKI 0x500).
   * android14-6.1 uses 0x400 (BTF reports 0x3c0). */
  uint32_t mm_struct_sz;
  uint32_t _pad[3];
};

#define OFFSETS_ENTRY(uname, ...) { .uname_r = uname, __VA_ARGS__ }

#define STRUCT_OFFSETS_6_1                                                     \
  .task_prio = 0x84, .task_normal_prio = 0x8C, .task_sched_task_group = 0x348, \
  .task_pi_lock = 0x924, .task_pi_waiters = 0x938,                             \
  .task_pi_top_task = 0x948, .task_pi_blocked_on = 0x950,                      \
  .task_pid = 0x630, .task_tgid = 0x634,                                       \
  .task_atomic_flags = 0x5F0, .task_real_cred = 0x830, .task_cred = 0x838,     \
  .task_comm = 0x848, .task_tasks = 0x550, .task_seccomp = 0x900,              \
  .compact_waiter = 1, .mm_struct_sz = 0x400, .kernel_phys_load = 0xa8000000

#define STRUCT_OFFSETS_6_12                                                    \
  .task_prio = 0x94, .task_normal_prio = 0x9C, .task_sched_task_group = 0x420, \
  .task_pi_lock = 0x9EC, .task_pi_waiters = 0xA00,                             \
  .task_pi_top_task = 0xA10, .task_pi_blocked_on = 0xA18,                      \
  .task_pid = 0x708, .task_tgid = 0x70C,                                       \
  .task_atomic_flags = 0x6C8, .task_real_cred = 0x8F8, .task_cred = 0x900,     \
  .task_comm = 0x910, .task_tasks = 0x638, .task_seccomp = 0x9C8,              \
  .kernel_phys_load = 0xc7800000

#define STRUCT_OFFSETS_6_6                                                     \
  .task_prio = 0x84, .task_normal_prio = 0x8C, .task_sched_task_group = 0x348, \
  .task_pi_lock = 0x90C, .task_pi_waiters = 0x920,                             \
  .task_pi_top_task = 0x930, .task_pi_blocked_on = 0x938,                      \
  .task_pid = 0x618, .task_tgid = 0x61C,                                       \
  .task_atomic_flags = 0x5D8, .task_real_cred = 0x818, .task_cred = 0x820,     \
  .task_comm = 0x830, .task_tasks = 0x550, .task_seccomp = 0x8E8,              \
  .kernel_phys_load = 0xa8000000

static const struct kernel_offsets known_offsets[] = {
/* Add new kernels by creating src/kernels/<uname-release>/offsets.h */
#include "6.1.118-android14-11-ga3b9c44908dd-ab13320413/offsets.h"
#include "6.1.118-android14-11-gca0ef6d17716-ab13624819/offsets.h"
#include "6.1.138-android14-11-g0c3d559bcd85-ab14529422/offsets.h"
#include "6.1.145-android14-11-g09f1c0074ad7-ab14226177/offsets.h"
#include "6.1.162-android14-11-gce140c0e5bf5-ab15450923/offsets.h"
#include "6.6.30-android15-8-g54dcbfbef792-ab12368803-4k/offsets.h"
#include "6.6.77-android15-8-g4a507830d890-ab13636293-4k/offsets.h"
#include "6.6.77-android15-8-g63ce7556864c-ab13994517-4k/offsets.h"
#include "6.6.77-android15-8-gca30f3b4bef6-abogki440974771-4k/offsets.h"
#include "6.6.89-android15-8-g8e4be6b47e40-ab14134548-4k/offsets.h"
#include "6.6.89-android15-8-g096cdb6ecefc-ab14358676-4k/offsets.h"
#include "6.6.89-android15-8-g0889fe95bb10-ab14402178-4k/offsets.h"
#include "6.6.89-android15-8-gf4dc45704e54-abogki446052083-4k/offsets.h"
#include "6.6.92-android15-8-g3637f4904cf5-ab13944661-4k/offsets.h"
#include "6.6.102-android15-8-gab8eb70a71b8-ab14350911-4k/offsets.h"
#include "6.6.102-android15-8-gb01b41c2647c-ab15574720-4k/offsets.h"
#include "6.6.102-android15-8-gfe76d1bc97fd-ab14689815-4k/offsets.h"
#include "6.6.118-android15-8-g2e6b9c3812c5-ab15114928-4k/offsets.h"
#include "6.6.118-android15-8-g93e223c276e7-abogki500782043-4k/offsets.h"
#include "6.6.118-android15-8-g608a629fedf7-ab15154340-4k/offsets.h"
#include "6.6.118-android15-8-gc44b714366cc-abogki519650608-4k/offsets.h"
#include "6.6.118-android15-8-ge56cf6b09cca-ab15511674-4k/offsets.h"
#include "6.6.118-android15-8-ge58033dc8ea6-abogki498046332-4k/offsets.h"
#include "6.6.118-android15-8-gebdfad32d749-ab15099304-4k/offsets.h"
#include "6.12.23-android16-5-g16e473de48a3-abogki462654244-4k/offsets.h"
#include "6.12.23-android16-5-g75e9b1c7ae7c-abogki463945075-4k/offsets.h"
#include "6.12.23-android16-5-g82efd98459a2-ab14457512-4k/offsets.h"
#include "6.12.23-android16-5-ga8f88ad96df3-ab13929693-4k/offsets.h"
#include "6.12.23-android16-5-gb2a876903b49-ab14541642-4k/offsets.h"
#include "6.12.23-android16-5-gf1bdb13583da-ab13761046-4k/offsets.h"
#include "6.12.30-android16-5-g6e872b4863d6-ab13847919-4k/offsets.h"
#include "6.12.38-android16-5-g3c4da6410bcb-ab13872285-4k/offsets.h"
#include "6.12.38-android16-5-g844001fb8721-ab14552068-4k/offsets.h"
  { .uname_r = NULL }
};

#endif
