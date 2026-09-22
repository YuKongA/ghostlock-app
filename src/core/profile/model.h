#ifndef GHOSTLOCK_PROFILE_H
#define GHOSTLOCK_PROFILE_H

#include <cstddef>
#include <cstdint>

#include <array>
#include <cstring>
#include <string_view>

namespace ghostlock::profile {
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
    enum class RouteKind : uint8_t {
        Auto = 0,
        TcpZerocopy = 1,
        SelectStack = 2,
        MulticastWaiter = 3,
    };

    /* Wire values for the binary / JSON transport layers. */
    inline constexpr uint8_t kRouteAuto = static_cast<uint8_t>(RouteKind::Auto);
    inline constexpr uint8_t kRouteTcpZerocopy =
            static_cast<uint8_t>(RouteKind::TcpZerocopy);
    inline constexpr uint8_t kRouteSelectStack =
            static_cast<uint8_t>(RouteKind::SelectStack);
    inline constexpr uint8_t kRouteMulticastWaiter =
            static_cast<uint8_t>(RouteKind::MulticastWaiter);

    /* Single native route catalog: token <-> wire value. Adding a route means
     * one entry here plus its RoutePolicy / procedure. */
    struct RouteCatalogEntry {
        std::string_view token;
        uint8_t wire;
    };

    inline constexpr RouteCatalogEntry kRouteCatalog[] = {
        {"tcp_zerocopy", kRouteTcpZerocopy},
        {"select_stack", kRouteSelectStack},
        {"multicast_waiter", kRouteMulticastWaiter},
    };

    [[nodiscard]] inline uint8_t route_kind_from_string(std::string_view name) {
        for (const RouteCatalogEntry &entry : kRouteCatalog) {
            if (name == entry.token) return entry.wire;
        }
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
        int32_t pselect_waiter_shift;
        int32_t mcast_waiter_off;
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
        /* Execution flags resolved from the profile (GLK1 v3). */
        uint8_t safe_mode;
        uint8_t multicast_resident;
        uint32_t mm_struct_sz;
        uint32_t _pad[3];
        struct execution_settings execution;

        /* Typed view of the wire route field so callers need no cast. */
        [[nodiscard]] RouteKind route_kind() const noexcept {
            return static_cast<RouteKind>(route);
        }
    };

    struct MulticastWaiterLayout {
        size_t waiter_offset, buffer_size, task_offset, lock_offset;
        size_t fake_lock_offset, fake_task_offset;
        size_t lock_slots_offset, lock_slot_count, lock_slot_stride;
        uint64_t fake_bss_image_offset;
    };

    struct SelectStackLayout {
        int32_t waiter_shift;
        int32_t compact_waiter;
    };

    struct TcpZerocopyLayout {
        int32_t compact_waiter;
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
            : TargetProfile(other) { // NOLINT(performance-move-constructor-init)
        }

        TargetProfile &operator=(TargetProfile &&other) noexcept {
            return *this = other;
        }

        [[nodiscard]] const struct kernel_offsets *values() const noexcept {
            return loaded_ ? &values_ : nullptr;
        }

        [[nodiscard]] bool loaded() const noexcept { return loaded_; }

        [[nodiscard]] RouteKind route() const noexcept {
            return loaded_ ? static_cast<RouteKind>(values_.route) : RouteKind::Auto;
        }

        [[nodiscard]] RouteKind fallback_route() const noexcept {
            return loaded_ ? static_cast<RouteKind>(values_.fallback_route) : RouteKind::Auto;
        }

        [[nodiscard]] bool supports(RouteKind kind) const noexcept {
            return route() == kind;
        }

        [[nodiscard]] const char *release() const noexcept {
            return loaded_ && values_.uname_r ? values_.uname_r : "";
        }

        [[nodiscard]] const struct execution_settings *execution() const noexcept {
            return loaded_ ? &values_.execution : nullptr;
        }

        /* Typed execution-config getters: return the value in the type callers
         * need, so no static_cast is required at the call site. */
#define GHOSTLOCK_EXEC_INT(name) \
        [[nodiscard]] int32_t name() const noexcept { return static_cast<int32_t>(values_.execution.name); }
#define GHOSTLOCK_EXEC_U32(name) \
        [[nodiscard]] uint32_t name() const noexcept { return values_.execution.name; }
        GHOSTLOCK_EXEC_INT(recommended_main_cpu)
        GHOSTLOCK_EXEC_INT(recommended_consumer_cpu)
        GHOSTLOCK_EXEC_INT(heap_prepare_max_attempts)
        GHOSTLOCK_EXEC_U32(heap_prepare_timeout_ms)
        GHOSTLOCK_EXEC_U32(heap_kernelsnitch_timeout_ms)
        GHOSTLOCK_EXEC_U32(race_route_wait_ms)
        GHOSTLOCK_EXEC_U32(race_setup_settle_us)
        GHOSTLOCK_EXEC_U32(race_state_poll_interval_us)
        GHOSTLOCK_EXEC_INT(w1_attempts)
        GHOSTLOCK_EXEC_U32(w1_settle_us)
        GHOSTLOCK_EXEC_INT(w1_scratch_repair_attempts)
        GHOSTLOCK_EXEC_INT(w2_attempts)
        GHOSTLOCK_EXEC_U32(w2_settle_us)
        GHOSTLOCK_EXEC_INT(w3_chain_rounds)
        GHOSTLOCK_EXEC_INT(w3_attempts)
        GHOSTLOCK_EXEC_U32(w3_settle_us)
        GHOSTLOCK_EXEC_INT(tcp_attempts)
        GHOSTLOCK_EXEC_INT(tcp_arm_sequence)
        GHOSTLOCK_EXEC_INT(tcp_post_receive_hold_iterations)
        GHOSTLOCK_EXEC_INT(select_enter_delay_us)
        GHOSTLOCK_EXEC_U32(select_timeout_us)
        GHOSTLOCK_EXEC_U32(select_consumer_max_calls)
        GHOSTLOCK_EXEC_U32(select_consumer_burst_calls)
        GHOSTLOCK_EXEC_U32(multicast_ready_timeout_ms)
        GHOSTLOCK_EXEC_U32(multicast_post_requeue_settle_us)
        GHOSTLOCK_EXEC_U32(multicast_post_adjust_settle_us)
        GHOSTLOCK_EXEC_U32(handoff_pre_dispatch_settle_ms)
        GHOSTLOCK_EXEC_U32(handoff_module_poll_attempts)
        GHOSTLOCK_EXEC_U32(handoff_module_poll_interval_ms)
        GHOSTLOCK_EXEC_U32(handoff_enforce_poll_attempts)
        GHOSTLOCK_EXEC_U32(handoff_enforce_poll_interval_ms)
#undef GHOSTLOCK_EXEC_INT
#undef GHOSTLOCK_EXEC_U32

        [[nodiscard]] bool has_compact_waiter() const noexcept {
            return loaded_ && values_.compact_waiter;
        }

        [[nodiscard]] bool safe_mode() const noexcept {
            return loaded_ && values_.safe_mode;
        }

        [[nodiscard]] bool multicast_resident() const noexcept {
            return loaded_ && values_.multicast_resident;
        }

        [[nodiscard]] MulticastWaiterLayout multicast_layout() const noexcept {
            return loaded_
                       ? (MulticastWaiterLayout){
                           .waiter_offset = static_cast<size_t>(values_.mcast_waiter_off),
                           .buffer_size = values_.mcast_buffer_size,
                           .task_offset = values_.mcast_task_offset,
                           .lock_offset = values_.mcast_lock_offset,
                           .fake_lock_offset = values_.mcast_fake_lock_offset,
                           .fake_task_offset = values_.mcast_fake_task_offset,
                           .lock_slots_offset = values_.mcast_lock_slots_offset,
                           .lock_slot_count = values_.mcast_lock_slot_count,
                           .lock_slot_stride = values_.mcast_lock_slot_stride,
                           .fake_bss_image_offset = values_.off_mcast_fake_bss,
                       }
                       : MulticastWaiterLayout{};
        }

        [[nodiscard]] SelectStackLayout select_stack_layout() const noexcept {
            return loaded_
                       ? (SelectStackLayout){
                           .waiter_shift = values_.pselect_waiter_shift,
                           .compact_waiter = values_.compact_waiter,
                       }
                       : SelectStackLayout{};
        }

        [[nodiscard]] TcpZerocopyLayout tcp_zerocopy_layout() const noexcept {
            return (TcpZerocopyLayout){.compact_waiter = has_compact_waiter()};
        }

        [[nodiscard]] uint32_t or_default(uint32_t value, uint32_t fallback)
        const noexcept {
            return (loaded_ && value) ? value : fallback;
        }

        [[nodiscard]] uint32_t mm_struct_stride(uint32_t fallback) const noexcept {
            return or_default(loaded_ ? values_.mm_struct_sz : 0, fallback);
        }

        [[nodiscard]] uint64_t image(uint64_t offset, uint64_t image_base,
                                     uint64_t fallback_offset) const noexcept {
            return image_base + ((loaded_ && offset) ? offset : fallback_offset);
        }

        static TargetProfile from(const struct kernel_offsets *values) {
            return values ? TargetProfile(*values) : TargetProfile();
        }

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
} // namespace ghostlock::profile

#endif
