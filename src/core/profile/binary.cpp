#include "profile/binary.h"

#include <cstring>
#include <iterator>
#include <string_view>
#include <type_traits>

namespace ghostlock::binary_profile {
    namespace {
        constexpr uint8_t kW8 = 8;
        constexpr uint8_t kVersion4 = 4;

        /* Reinterpret one transport member as the wire's raw 64-bit record.
         * Signed members are sign-extended exactly like the previous
         * reinterpret_cast implementation did. */
        template<typename T>
        constexpr uint64_t to_raw(T value) noexcept {
            if constexpr (std::is_signed_v<T>) {
                return static_cast<uint64_t>(static_cast<int64_t>(value));
            } else {
                return static_cast<uint64_t>(value);
            }
        }

        template<typename T>
        constexpr T from_raw(uint64_t raw) noexcept {
            if constexpr (std::is_signed_v<T>) {
                return static_cast<T>(static_cast<int64_t>(raw));
            } else {
                return static_cast<T>(raw);
            }
        }

        /* One typed read/write pair over the transport struct. */
        struct Field {
            uint64_t (*load)(const profile::kernel_offsets &);

            void (*store)(profile::kernel_offsets &, uint64_t);
        };

#define FIELD(member)                                                \
    {                                                               \
        [](const profile::kernel_offsets &o) -> uint64_t {            \
            return to_raw(o.member);                                \
        },                                                          \
        [](profile::kernel_offsets &o, uint64_t raw) {                \
            o.member = from_raw<decltype(o.member)>(raw);           \
        }                                                           \
    }

        /* GLK1 v4 = fixed common slots + a route section. The common slots hold
         * only route-independent members; every route-specific member is one
         * name->u64 entry in the route section, decoded by that route's table.
         * Adding a route means adding a route table, not enlarging the common
         * schema. Both sides derive from the same names/order. */
        constexpr Field kCommonFields[] = {
            /* task_struct */
            FIELD(task_prio), FIELD(task_normal_prio), FIELD(task_sched_task_group),
            FIELD(task_pi_lock), FIELD(task_pi_waiters), FIELD(task_pi_top_task),
            FIELD(task_pi_blocked_on), FIELD(task_pid), FIELD(task_tgid),
            FIELD(task_atomic_flags), FIELD(task_real_cred), FIELD(task_cred),
            FIELD(task_comm), FIELD(task_tasks), FIELD(task_seccomp),
            /* cred */
            FIELD(cred_copy_size), FIELD(cred_usage_offset), FIELD(cred_usage_value),
            FIELD(cred_caps_offset), FIELD(cred_caps_count), FIELD(cred_caps_value),
            FIELD(cred_ref_count), FIELD(cred_ref0_offset), FIELD(cred_ref1_offset),
            FIELD(cred_ref2_offset), FIELD(cred_ref3_offset), FIELD(cred_ref0_image),
            FIELD(cred_ref1_image), FIELD(cred_ref2_image), FIELD(cred_ref3_image),
            /* offset (route-independent kernel anchors) */
            FIELD(off_init_task), FIELD(off_init_cred), FIELD(off_empty_zero_page),
            FIELD(off_root_task_group), FIELD(off_selinux_enforcing),
            FIELD(off_selinux_blob_sizes), FIELD(off_security_hook_heads),
            FIELD(off_slide_nfulnl_logger), FIELD(off_slide_loggers_0_1),
            FIELD(off_slide_boot_id),
            /* misc */
            FIELD(kernel_phys_load), FIELD(compact_waiter),
            FIELD(kernelsnitch_collisions), FIELD(mm_struct_sz),
            /* execution tuning (route-independent) */
            FIELD(execution.recommended_main_cpu),
            FIELD(execution.recommended_consumer_cpu),
            FIELD(execution.heap_prepare_max_attempts),
            FIELD(execution.heap_prepare_timeout_ms),
            FIELD(execution.heap_kernelsnitch_timeout_ms),
            FIELD(execution.race_route_wait_ms),
            FIELD(execution.race_setup_settle_us),
            FIELD(execution.race_state_poll_interval_us),
            FIELD(execution.w1_attempts), FIELD(execution.w1_settle_us),
            FIELD(execution.w1_scratch_repair_attempts),
            FIELD(execution.w2_attempts), FIELD(execution.w2_settle_us),
            FIELD(execution.w3_chain_rounds), FIELD(execution.w3_attempts),
            FIELD(execution.w3_settle_us),
            FIELD(execution.handoff_pre_dispatch_settle_ms),
            FIELD(execution.handoff_module_poll_attempts),
            FIELD(execution.handoff_module_poll_interval_ms),
            FIELD(execution.handoff_enforce_poll_attempts),
            FIELD(execution.handoff_enforce_poll_interval_ms),
            /* execution flags */
            FIELD(safe_mode),
        };

        struct NamedField {
            const char *name;
            Field field;
        };

        constexpr NamedField kTcpFields[] = {
            {"tcp_attempts", FIELD(execution.tcp_attempts)},
            {"tcp_arm_sequence", FIELD(execution.tcp_arm_sequence)},
            {"tcp_post_receive_hold_iterations",
             FIELD(execution.tcp_post_receive_hold_iterations)},
        };

        constexpr NamedField kSelectFields[] = {
            {"pselect_waiter_shift", FIELD(pselect_waiter_shift)},
            {"select_enter_delay_us", FIELD(execution.select_enter_delay_us)},
            {"select_timeout_us", FIELD(execution.select_timeout_us)},
            {"select_consumer_max_calls", FIELD(execution.select_consumer_max_calls)},
            {"select_consumer_burst_calls", FIELD(execution.select_consumer_burst_calls)},
        };

        constexpr NamedField kMulticastFields[] = {
            {"mcast_waiter_off", FIELD(mcast_waiter_off)},
            {"mcast_buffer_size", FIELD(mcast_buffer_size)},
            {"mcast_task_offset", FIELD(mcast_task_offset)},
            {"mcast_lock_offset", FIELD(mcast_lock_offset)},
            {"mcast_fake_lock_offset", FIELD(mcast_fake_lock_offset)},
            {"mcast_fake_task_offset", FIELD(mcast_fake_task_offset)},
            {"mcast_lock_slots_offset", FIELD(mcast_lock_slots_offset)},
            {"mcast_lock_slot_count", FIELD(mcast_lock_slot_count)},
            {"mcast_lock_slot_stride", FIELD(mcast_lock_slot_stride)},
            {"off_mcast_fake_bss", FIELD(off_mcast_fake_bss)},
            {"multicast_resident", FIELD(multicast_resident)},
            {"multicast_ready_timeout_ms", FIELD(execution.multicast_ready_timeout_ms)},
            {"multicast_post_requeue_settle_us",
             FIELD(execution.multicast_post_requeue_settle_us)},
            {"multicast_post_adjust_settle_us",
             FIELD(execution.multicast_post_adjust_settle_us)},
        };
#undef FIELD

        struct RouteFields {
            uint8_t route;
            const NamedField *fields;
            size_t count;
        };

        constexpr RouteFields kRouteFields[] = {
            {profile::kRouteTcpZerocopy, kTcpFields, std::size(kTcpFields)},
            {profile::kRouteSelectStack, kSelectFields, std::size(kSelectFields)},
            {profile::kRouteMulticastWaiter, kMulticastFields, std::size(kMulticastFields)},
        };

        const RouteFields *route_fields(uint8_t route) {
            for (const RouteFields &rf: kRouteFields) {
                if (rf.route == route) return &rf;
            }
            return nullptr;
        }

        constexpr size_t kCommonCount = std::size(kCommonFields);
        constexpr size_t kHeaderSize = 12;

        uint64_t read_le(const uint8_t *bytes, size_t width) {
            uint64_t value = 0;
            for (size_t i = 0; i < width; i++) {
                value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
            }
            return value;
        }

        void write_le(uint8_t *bytes, uint64_t value, size_t width) {
            for (size_t i = 0; i < width; i++) {
                bytes[i] = static_cast<uint8_t>(value >> (8 * i));
            }
        }
    } // namespace

    int32_t parse(std::string_view document, profile::kernel_offsets *out,
              char *release_buf, size_t release_buf_cap) {
        if (!out || !release_buf || document.size() < kHeaderSize) return -1;
        const auto *bytes = reinterpret_cast<const uint8_t *>(document.data());
        const auto *end = bytes + document.size();
        if (read_le(bytes, 4) != kMagic || read_le(bytes + 4, 2) != kVersion4) {
            return -1;
        }
        const size_t release_length = static_cast<size_t>(read_le(bytes + 10, 2));
        const size_t common_end = kHeaderSize + release_length + kCommonCount * kW8;
        if (document.size() < common_end) return -1;
        if (release_length + 1 > release_buf_cap) return -1;
        memcpy(release_buf, bytes + kHeaderSize, release_length);
        release_buf[release_length] = '\0';

        memset(out, 0, sizeof(*out));
        out->uname_r = release_buf;
        out->route = bytes[6];
        /* The route is profile-controlled: an unresolved kRouteAuto is rejected
         * instead of being inferred from kernel geometry. */
        if (out->route == profile::kRouteAuto) return -1;
        out->kernel_major = bytes[7];
        out->recommend_shizuku = bytes[8];
        out->fallback_route = bytes[9];

        const uint8_t *common = bytes + kHeaderSize + release_length;
        for (size_t i = 0; i < kCommonCount; i++) {
            kCommonFields[i].store(*out, read_le(common + i * kW8, kW8));
        }

        const RouteFields *rf = route_fields(out->route);
        if (!rf) return -1;
        const uint8_t *p = common + kCommonCount * kW8;
        if (p >= end) return -1;
        const size_t count = *p++;
        for (size_t e = 0; e < count; e++) {
            if (p + 1 > end) return -1;
            const size_t key_len = *p++;
            if (p + key_len + kW8 > end) return -1;
            const std::string_view key(reinterpret_cast<const char *>(p), key_len);
            const uint64_t raw = read_le(p + key_len, kW8);
            for (size_t i = 0; i < rf->count; i++) {
                if (key == rf->fields[i].name) {
                    rf->fields[i].field.store(*out, raw);
                    break;
                }
            }
            p += key_len + kW8;
        }
        return 0;
    }

    int32_t serialize(const profile::kernel_offsets *in, char *buffer, size_t capacity) {
        if (!in || !buffer || !in->uname_r) return -1;
        if (in->route == profile::kRouteAuto) return -1;
        const RouteFields *rf = route_fields(in->route);
        if (!rf) return -1;
        const size_t release_length = strlen(in->uname_r);
        if (release_length > 0xffff) return -1;
        size_t route_bytes = 1; /* count */
        for (size_t i = 0; i < rf->count; i++) {
            route_bytes += 1 + strlen(rf->fields[i].name) + kW8;
        }
        const size_t total =
                kHeaderSize + release_length + kCommonCount * kW8 + route_bytes;
        if (total > capacity) return -1;

        auto *bytes = reinterpret_cast<uint8_t *>(buffer);
        memset(bytes, 0, kHeaderSize);
        write_le(bytes, kMagic, 4);
        write_le(bytes + 4, kVersion4, 2);
        bytes[6] = in->route;
        bytes[7] = in->kernel_major;
        bytes[8] = in->recommend_shizuku;
        bytes[9] = in->fallback_route;
        write_le(bytes + 10, release_length, 2);
        memcpy(bytes + kHeaderSize, in->uname_r, release_length);

        uint8_t *common = bytes + kHeaderSize + release_length;
        for (size_t i = 0; i < kCommonCount; i++) {
            write_le(common + i * kW8, kCommonFields[i].load(*in), kW8);
        }

        uint8_t *p = common + kCommonCount * kW8;
        *p++ = static_cast<uint8_t>(rf->count);
        for (size_t i = 0; i < rf->count; i++) {
            const size_t key_len = strlen(rf->fields[i].name);
            *p++ = static_cast<uint8_t>(key_len);
            memcpy(p, rf->fields[i].name, key_len);
            p += key_len;
            write_le(p, rf->fields[i].field.load(*in), kW8);
            p += kW8;
        }
        return static_cast<int32_t>(p - bytes);
    }
} // namespace ghostlock::binary_profile
