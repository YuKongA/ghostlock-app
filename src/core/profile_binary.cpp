#include "profile_binary.h"

#include <string.h>

namespace ghostlock::binary_profile {
namespace {

constexpr uint8_t kW1 = 1;
constexpr uint8_t kW4 = 4;
constexpr uint8_t kW8 = 8;

struct Field {
    size_t offset;
    uint8_t width;
    bool is_signed;
};

/* Fixed order shared with Kotlin's NativeProfileDocument.flatten(). */
#define F(name, width, sign) {offsetof(struct kernel_offsets, name), width, sign}
constexpr Field kFields[] = {
        /* task_struct */
        F(task_prio, kW4, false),
        F(task_normal_prio, kW4, false),
        F(task_sched_task_group, kW4, false),
        F(task_pi_lock, kW4, false),
        F(task_pi_waiters, kW4, false),
        F(task_pi_top_task, kW4, false),
        F(task_pi_blocked_on, kW4, false),
        F(task_pid, kW4, false),
        F(task_tgid, kW4, false),
        F(task_atomic_flags, kW4, false),
        F(task_real_cred, kW4, false),
        F(task_cred, kW4, false),
        F(task_comm, kW4, false),
        F(task_tasks, kW4, false),
        F(task_seccomp, kW4, false),
        /* cred */
        F(cred_copy_size, kW4, false),
        F(cred_usage_offset, kW4, false),
        F(cred_usage_value, kW4, false),
        F(cred_caps_offset, kW4, false),
        F(cred_caps_count, kW4, false),
        F(cred_caps_value, kW8, false),
        F(cred_ref_count, kW4, false),
        F(cred_ref0_offset, kW4, false),
        F(cred_ref1_offset, kW4, false),
        F(cred_ref2_offset, kW4, false),
        F(cred_ref3_offset, kW4, false),
        F(cred_ref0_image, kW8, false),
        F(cred_ref1_image, kW8, false),
        F(cred_ref2_image, kW8, false),
        F(cred_ref3_image, kW8, false),
        /* offset (kernel symbols and slide anchors) */
        F(off_init_task, kW8, false),
        F(off_init_cred, kW8, false),
        F(off_empty_zero_page, kW8, false),
        F(off_mcast_fake_bss, kW8, false),
        F(off_root_task_group, kW8, false),
        F(off_selinux_enforcing, kW8, false),
        F(off_selinux_blob_sizes, kW8, false),
        F(off_security_hook_heads, kW8, false),
        F(off_slide_nfulnl_logger, kW8, false),
        F(off_slide_loggers_0_1, kW8, false),
        F(off_slide_boot_id, kW8, false),
        /* mcast geometry */
        F(mcast_waiter_off, kW4, true),
        F(mcast_buffer_size, kW4, false),
        F(mcast_task_offset, kW4, false),
        F(mcast_lock_offset, kW4, false),
        F(mcast_fake_lock_offset, kW4, false),
        F(mcast_fake_task_offset, kW4, false),
        F(mcast_lock_slots_offset, kW4, false),
        F(mcast_lock_slot_count, kW4, false),
        F(mcast_lock_slot_stride, kW4, false),
        /* misc */
        F(kernel_phys_load, kW8, false),
        F(pselect_waiter_shift, kW4, true),
        F(compact_waiter, kW1, false),
        F(kernelsnitch_collisions, kW4, false),
        F(mm_struct_sz, kW4, false),
        /* execution tuning */
        F(execution.recommended_main_cpu, kW4, false),
        F(execution.recommended_consumer_cpu, kW4, false),
        F(execution.heap_prepare_max_attempts, kW4, false),
        F(execution.heap_prepare_timeout_ms, kW4, false),
        F(execution.heap_kernelsnitch_timeout_ms, kW4, false),
        F(execution.race_route_wait_ms, kW4, false),
        F(execution.race_setup_settle_us, kW4, false),
        F(execution.race_state_poll_interval_us, kW4, false),
        F(execution.w1_attempts, kW4, false),
        F(execution.w1_settle_us, kW4, false),
        F(execution.w1_scratch_repair_attempts, kW4, false),
        F(execution.w2_attempts, kW4, false),
        F(execution.w2_settle_us, kW4, false),
        F(execution.w3_chain_rounds, kW4, false),
        F(execution.w3_attempts, kW4, false),
        F(execution.w3_settle_us, kW4, false),
        F(execution.tcp_attempts, kW4, false),
        F(execution.tcp_arm_sequence, kW4, false),
        F(execution.tcp_post_receive_hold_iterations, kW4, false),
        F(execution.select_enter_delay_us, kW4, false),
        F(execution.select_timeout_us, kW4, false),
        F(execution.select_consumer_max_calls, kW4, false),
        F(execution.select_consumer_burst_calls, kW4, false),
        F(execution.multicast_ready_timeout_ms, kW4, false),
        F(execution.multicast_post_requeue_settle_us, kW4, false),
        F(execution.multicast_post_adjust_settle_us, kW4, false),
        F(execution.handoff_pre_dispatch_settle_ms, kW4, false),
        F(execution.handoff_module_poll_attempts, kW4, false),
        F(execution.handoff_module_poll_interval_ms, kW4, false),
        F(execution.handoff_enforce_poll_attempts, kW4, false),
        F(execution.handoff_enforce_poll_interval_ms, kW4, false),
        /* execution flags (GLK1 v3) */
        F(safe_mode, kW1, false),
        F(multicast_resident, kW1, false),
};
#undef F

constexpr size_t kFieldCount = sizeof(kFields) / sizeof(kFields[0]);
constexpr size_t kHeaderSize = 12;

uint64_t read_le(const uint8_t *bytes, size_t width) {
    uint64_t value = 0;
    for (size_t i = 0; i < width; i++) {
        value |= (uint64_t) bytes[i] << (8 * i);
    }
    return value;
}

void write_le(uint8_t *bytes, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; i++) {
        bytes[i] = (uint8_t) (value >> (8 * i));
    }
}

int64_t as_signed(uint64_t value, size_t width) {
    const size_t bits = width * 8;
    if (bits >= 64) return (int64_t) value;
    const uint64_t sign_bit = 1ULL << (bits - 1);
    return (int64_t) ((value ^ sign_bit) - sign_bit);
}

}  // namespace

int parse(std::string_view document, struct kernel_offsets *out,
        char *release_buf, size_t release_buf_cap) {
    if (!out || !release_buf || document.size() < kHeaderSize) return -1;
    const auto *bytes = reinterpret_cast<const uint8_t *>(document.data());
    if (read_le(bytes, 4) != kMagic || read_le(bytes + 4, 2) != kVersion) {
        return -1;
    }
    const size_t release_length = (size_t) read_le(bytes + 10, 2);
    if (document.size() != kHeaderSize + release_length + kFieldCount * kW8) {
        return -1;
    }
    if (release_length + 1 > release_buf_cap) return -1;
    memcpy(release_buf, bytes + kHeaderSize, release_length);
    release_buf[release_length] = '\0';

    memset(out, 0, sizeof(*out));
    out->uname_r = release_buf;
    out->route = bytes[6];
    /* The route is profile-controlled: an unresolved kRouteAuto is rejected
     * instead of being inferred from kernel geometry. */
    if (out->route == kRouteAuto) return -1;
    out->kernel_major = bytes[7];
    out->recommend_shizuku = bytes[8];
    out->fallback_route = bytes[9];

    const uint8_t *fields = bytes + kHeaderSize + release_length;
    for (size_t i = 0; i < kFieldCount; i++) {
        const uint64_t raw = read_le(fields + i * kW8, kW8);
        const Field &field = kFields[i];
        void *target = reinterpret_cast<char *>(out) + field.offset;
        if (field.width == kW1) {
            *reinterpret_cast<uint8_t *>(target) = (uint8_t) raw;
        } else if (field.width == kW4) {
            *reinterpret_cast<uint32_t *>(target) = field.is_signed
                    ? (uint32_t) as_signed(raw, kW4)
                    : (uint32_t) raw;
        } else {
            *reinterpret_cast<uint64_t *>(target) = raw;
        }
    }
    return 0;
}

int serialize(const struct kernel_offsets *in, char *buffer, size_t capacity) {
    if (!in || !buffer || !in->uname_r) return -1;
    if (in->route == kRouteAuto) return -1;
    const size_t release_length = strlen(in->uname_r);
    if (release_length > 0xffff) return -1;
    const size_t total = kHeaderSize + release_length + kFieldCount * kW8;
    if (total > capacity) return -1;

    auto *bytes = reinterpret_cast<uint8_t *>(buffer);
    memset(bytes, 0, kHeaderSize);
    write_le(bytes, kMagic, 4);
    write_le(bytes + 4, kVersion, 2);
    bytes[6] = in->route;
    bytes[7] = in->kernel_major;
    bytes[8] = in->recommend_shizuku;
    bytes[9] = in->fallback_route;
    write_le(bytes + 10, release_length, 2);
    memcpy(bytes + kHeaderSize, in->uname_r, release_length);

    uint8_t *fields = bytes + kHeaderSize + release_length;
    for (size_t i = 0; i < kFieldCount; i++) {
        const Field &field = kFields[i];
        const void *source = reinterpret_cast<const char *>(in) + field.offset;
        uint64_t raw;
        if (field.width == kW1) {
            raw = *reinterpret_cast<const uint8_t *>(source);
        } else if (field.width == kW4) {
            raw = field.is_signed
                    ? (uint64_t) (int64_t) *reinterpret_cast<const int32_t *>(source)
                    : (uint64_t) *reinterpret_cast<const uint32_t *>(source);
        } else {
            raw = *reinterpret_cast<const uint64_t *>(source);
        }
        write_le(fields + i * kW8, raw, kW8);
    }
    return (int) total;
}

}  // namespace ghostlock::binary_profile
