#include "profile_binary.h"

#include <cstring>
#include <type_traits>

namespace ghostlock::binary_profile {
    using namespace ghostlock::profile;

    namespace {
        constexpr uint8_t kW8 = 8;

        /* Reinterpret one transport member as the wire's raw 64-bit record.
         * Signed members are sign-extended exactly like the previous
         * reinterpret_cast implementation did. */
        template <typename T>
        constexpr uint64_t to_raw(T value) noexcept {
            if constexpr (std::is_signed_v<T>) {
                return static_cast<uint64_t>(static_cast<int64_t>(value));
            } else {
                return static_cast<uint64_t>(value);
            }
        }

        template <typename T>
        constexpr T from_raw(uint64_t raw) noexcept {
            if constexpr (std::is_signed_v<T>) {
                return static_cast<T>(static_cast<int64_t>(raw));
            } else {
                return static_cast<T>(raw);
            }
        }

        /* One typed read/write pair over the transport struct. The order of
         * kFields is the GLK1 wire order shared with Kotlin's
         * NativeProfileDocument.flatten(); every record is 8 bytes, so no
         * offsetof or reinterpret_cast is needed to reach a member. */
        struct Field {
            uint64_t (*load)(const struct kernel_offsets &);
            void (*store)(struct kernel_offsets &, uint64_t);
        };

#define FIELD(member)                                                \
    {                                                               \
        [](const struct kernel_offsets &o) -> uint64_t {            \
            return to_raw(o.member);                                \
        },                                                          \
        [](struct kernel_offsets &o, uint64_t raw) {                \
            o.member = from_raw<decltype(o.member)>(raw);           \
        }                                                           \
    }
        constexpr Field kFields[] = {
            /* task_struct */
            FIELD(task_prio),
            FIELD(task_normal_prio),
            FIELD(task_sched_task_group),
            FIELD(task_pi_lock),
            FIELD(task_pi_waiters),
            FIELD(task_pi_top_task),
            FIELD(task_pi_blocked_on),
            FIELD(task_pid),
            FIELD(task_tgid),
            FIELD(task_atomic_flags),
            FIELD(task_real_cred),
            FIELD(task_cred),
            FIELD(task_comm),
            FIELD(task_tasks),
            FIELD(task_seccomp),
            /* cred */
            FIELD(cred_copy_size),
            FIELD(cred_usage_offset),
            FIELD(cred_usage_value),
            FIELD(cred_caps_offset),
            FIELD(cred_caps_count),
            FIELD(cred_caps_value),
            FIELD(cred_ref_count),
            FIELD(cred_ref0_offset),
            FIELD(cred_ref1_offset),
            FIELD(cred_ref2_offset),
            FIELD(cred_ref3_offset),
            FIELD(cred_ref0_image),
            FIELD(cred_ref1_image),
            FIELD(cred_ref2_image),
            FIELD(cred_ref3_image),
            /* offset (kernel symbols and slide anchors) */
            FIELD(off_init_task),
            FIELD(off_init_cred),
            FIELD(off_empty_zero_page),
            FIELD(off_mcast_fake_bss),
            FIELD(off_root_task_group),
            FIELD(off_selinux_enforcing),
            FIELD(off_selinux_blob_sizes),
            FIELD(off_security_hook_heads),
            FIELD(off_slide_nfulnl_logger),
            FIELD(off_slide_loggers_0_1),
            FIELD(off_slide_boot_id),
            /* mcast geometry */
            FIELD(mcast_waiter_off),
            FIELD(mcast_buffer_size),
            FIELD(mcast_task_offset),
            FIELD(mcast_lock_offset),
            FIELD(mcast_fake_lock_offset),
            FIELD(mcast_fake_task_offset),
            FIELD(mcast_lock_slots_offset),
            FIELD(mcast_lock_slot_count),
            FIELD(mcast_lock_slot_stride),
            /* misc */
            FIELD(kernel_phys_load),
            FIELD(pselect_waiter_shift),
            FIELD(compact_waiter),
            FIELD(kernelsnitch_collisions),
            FIELD(mm_struct_sz),
            /* execution tuning */
            FIELD(execution.recommended_main_cpu),
            FIELD(execution.recommended_consumer_cpu),
            FIELD(execution.heap_prepare_max_attempts),
            FIELD(execution.heap_prepare_timeout_ms),
            FIELD(execution.heap_kernelsnitch_timeout_ms),
            FIELD(execution.race_route_wait_ms),
            FIELD(execution.race_setup_settle_us),
            FIELD(execution.race_state_poll_interval_us),
            FIELD(execution.w1_attempts),
            FIELD(execution.w1_settle_us),
            FIELD(execution.w1_scratch_repair_attempts),
            FIELD(execution.w2_attempts),
            FIELD(execution.w2_settle_us),
            FIELD(execution.w3_chain_rounds),
            FIELD(execution.w3_attempts),
            FIELD(execution.w3_settle_us),
            FIELD(execution.tcp_attempts),
            FIELD(execution.tcp_arm_sequence),
            FIELD(execution.tcp_post_receive_hold_iterations),
            FIELD(execution.select_enter_delay_us),
            FIELD(execution.select_timeout_us),
            FIELD(execution.select_consumer_max_calls),
            FIELD(execution.select_consumer_burst_calls),
            FIELD(execution.multicast_ready_timeout_ms),
            FIELD(execution.multicast_post_requeue_settle_us),
            FIELD(execution.multicast_post_adjust_settle_us),
            FIELD(execution.handoff_pre_dispatch_settle_ms),
            FIELD(execution.handoff_module_poll_attempts),
            FIELD(execution.handoff_module_poll_interval_ms),
            FIELD(execution.handoff_enforce_poll_attempts),
            FIELD(execution.handoff_enforce_poll_interval_ms),
            /* execution flags (GLK1 v3) */
            FIELD(safe_mode),
            FIELD(multicast_resident),
        };
#undef FIELD

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
                bytes[i] = (uint8_t)(value >> (8 * i));
            }
        }
    } // namespace

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
            kFields[i].store(*out, read_le(fields + i * kW8, kW8));
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
            write_le(fields + i * kW8, kFields[i].load(*in), kW8);
        }
        return (int) total;
    }
} // namespace ghostlock::binary_profile
