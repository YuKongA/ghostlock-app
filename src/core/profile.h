#ifndef GHOSTLOCK_PROFILE_H
#define GHOSTLOCK_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#include <array>
#include <cstring>
#include <string_view>

/* PROFILE-SUGGEST-01: only kernel geometry (kernel_major, symbol and struct
 * offsets, waiter layout, credential template) is truly required. Kotlin merges
 * the shipped execution defaults and user overrides before the profile reaches
 * native; see docs/kernel_profiles/defaults*.md. */
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


/* Explicit route selection written by the profile ("route": "<name>").
 * Geometry inference below exists only for profiles predating the field. */
enum : uint8_t {
  kRouteAuto = 0,
  kRouteTcpZerocopy = 1,
  kRouteSelectStack = 2,
  kRouteMulticastWaiter = 3,
};

[[nodiscard]] inline uint8_t route_kind_from_string(std::string_view name) {
  if (name == "tcp_zerocopy") return kRouteTcpZerocopy;
  if (name == "select_stack") return kRouteSelectStack;
  if (name == "multicast_waiter") return kRouteMulticastWaiter;
  return kRouteAuto;
}

/* Native transport representation of one Kotlin-resolved JSON profile.
 * S08 wraps this compatibility layout in an immutable TargetProfile. */
struct kernel_offsets {
  const char *uname_r;
  uint8_t kernel_major;
  uint8_t recommend_shizuku;
  uint8_t route;
  /* Declared fallback route ("fallback_to": "none"/"<route>"); kRouteAuto
   * means none. tcp_zerocopy currently falls back to select_stack. */
  uint8_t fallback_route;
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

  /* Delegates to the copy constructor on purpose: rebinds the release buffer
   * and keeps the moved-from profile valid for the process-wide accessor. */
  TargetProfile(TargetProfile &&other) noexcept
      : TargetProfile(other) {}  // NOLINT(performance-move-constructor-init)

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
  return values ? TargetProfile(*values) : TargetProfile();
}

static inline const struct kernel_offsets *
target_profile_values(const TargetProfile *profile) {
  return profile ? profile->values() : nullptr;
}

static inline int target_profile_is_loaded(const TargetProfile *profile) {
  return target_profile_values(profile) != nullptr;
}

static inline const struct execution_settings *
target_profile_execution(const TargetProfile *profile) {
  const struct kernel_offsets *values = target_profile_values(profile);
  return values ? &values->execution : nullptr;
}

/* The effective route: the explicit field wins, otherwise fall back to the
 * legacy geometry inference so old profiles keep behaving the same way. */
static inline uint8_t target_profile_route(const TargetProfile *profile) {
  const struct kernel_offsets *v = target_profile_values(profile);
  if (!v) return kRouteAuto;
  if (v->route != kRouteAuto) return v->route;
  if (v->kernel_major == 5 && v->mcast_waiter_off > 0) {
    return kRouteMulticastWaiter;
  }
  if (v->compact_waiter) return kRouteTcpZerocopy;
  return kRouteSelectStack;
}

static inline int target_profile_supports_multicast_waiter(
    const TargetProfile *profile) {
  return target_profile_route(profile) == kRouteMulticastWaiter;
}

static inline int target_profile_supports_tcp_zerocopy(
    const TargetProfile *profile) {
  return target_profile_route(profile) == kRouteTcpZerocopy;
}

static inline int target_profile_supports_select_stack(
    const TargetProfile *profile) {
  return target_profile_route(profile) == kRouteSelectStack;
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
  } : MulticastWaiterLayout{};
}

static inline SelectStackLayout target_profile_select_stack_layout(
    const TargetProfile *profile) {
  const struct kernel_offsets *v = target_profile_values(profile);
  return v ? (SelectStackLayout){
      .waiter_shift = v->pselect_waiter_shift,
      .compact_waiter = v->compact_waiter,
  } : SelectStackLayout{};
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
