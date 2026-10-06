#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_SCHEMA_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_SCHEMA_HPP

/* Owner schema for the cve_2026_43499 backend keys (ADR-0003 / A2-3c-2,
 * split by A2-4-3).
 *
 * kFields declares exactly the (section, key) pairs this owner *interprets*:
 * the step set, the credential template values, the KASLR slide targets, the
 * route/spray geometry, execution tuning and the 43499 policy meta. The
 * platform ABI keys (task_struct layout, cred layout offsets, symbol offsets,
 * device phys) moved to platform::abi (platform/abi.hpp); composition binds
 * both owners and merges the platform View into the frozen transport (see
 * backend_profile.cpp).
 *
 * backend.cve_2026_43499.steps is a backend-private key: it selects the step
 * set (ADR-0004 R18), not a kernel offset, so it binds to View::steps instead
 * of a kernel_offsets member. Declaring it here is what lets a strict
 * (Production) bind accept a real document rather than rejecting the private
 * section as unknown.
 *
 * kernel_offsets/TargetProfile stay physically frozen, so this View wraps the
 * transport instead of replacing it. The legacy v2 serializer was removed in
 * S4-R2c, so the GLKv3 FieldSpec/manifest is now the single authority for
 * these declarations. */

#include "backend/cve_2026_43499/backend_profile/model.hpp"
#include "contract/identity.hpp"
#include "platform/abi.hpp"
#include "profile/registry.hpp"
#include "profile/schema.hpp"

#include <cstdint>
#include <type_traits>

namespace ghostlock::backend {
    /* 43499 View: the frozen transport plus the backend-private step-set id. */
    struct Cve2026_43499View {
        profile::kernel_offsets values;
        uint16_t steps = 0;
    };

    namespace schema_detail {
        template<typename T>
        [[nodiscard]] constexpr T from_raw(uint64_t raw) noexcept {
            if constexpr (std::is_signed_v<T>) {
                return static_cast<T>(static_cast<int64_t>(raw));
            } else {
                return static_cast<T>(raw);
            }
        }
    } // namespace schema_detail

    using Cve2026_43499Field = profile::FieldSpec<Cve2026_43499View>;

#define GLK_43499_PLAIN(section, key, member, width) \
    { \
        section, key, width, false, false, \
                [](Cve2026_43499View &view, uint64_t raw) { \
                    view.values.member = \
                            schema_detail::from_raw< \
                                    decltype(view.values.member)>(raw); \
                } \
    }

/* Optional field: absence is meaningful, so the whole optional is assigned. */
#define GLK_43499_OPT(section, key, member, width, sign) \
    { \
        section, key, width, sign, false, \
                [](Cve2026_43499View &view, uint64_t raw) { \
                    view.values.member = schema_detail::from_raw< \
                            std::remove_reference_t< \
                                    decltype(*view.values.member)>>(raw); \
                } \
    }

/* Backend-private combination token (S4 R6b); never touches kernel_offsets.
 * The token is stringified; the internal numeric step-set id is derived from
 * the shared contract whitelist so there is one vocabulary authority. A legacy
 * numeric value is rewritten to its token by profile/glkv3_parse.cpp before the
 * bind runs, so this field can remain a strict String. */
/* M2 item 6: a selection-owned declaration row. The key is accepted on the wire,
 * validated by kind and shape, and never stored into a View member (the queue,
 * the queue-level route and the experimental declaration are consumed by
 * profile/glkv3_parse.cpp). */
#define GLK_43499_SELECTION(section, key, wire, width, doc_text) \
    { \
        section, key, width, false, false, nullptr, \
                profile::DefaultValue::none(), profile::FieldSource::Profile, \
                wire, doc_text, nullptr, nullptr, true \
    }

#define GLK_43499_STEPS(section, key) \
    { \
        section, key, 0, false, false, nullptr, \
                profile::DefaultValue::none(), profile::FieldSource::Profile, \
                profile::WireKind::String, \
                "Combination token <route>_<path> (S4 R6b).", \
                nullptr, \
                [](Cve2026_43499View &view, std::string_view text) { \
                    return contract::combination_stepset_wire_checked( \
                            contract::BackendKind::Cve2026_43499, text, \
                            view.steps); \
                } \
    }

    struct Cve2026_43499Schema {
        using View = Cve2026_43499View;

        static constexpr Cve2026_43499Field kFields[] = {
            /* HOCON refactor root scalars: the empty section is the document
             * root (profile/document.hpp kRootSection), where the wire and the
             * profile now agree. They replace the deleted "common" owner;
             * common.vr_guard went with it and has no writer left (and the
             * platform/vivo consumer was deleted in vr_guard (a)). */
            GLK_43499_PLAIN("", "kernel_major", meta.kernel_major, 1),
            GLK_43499_PLAIN("", "kernel_minor", meta.kernel_minor, 1),
            GLK_43499_PLAIN("", "safe_mode", meta.safe_mode, 1),
            GLK_43499_PLAIN("backend.cve_2026_43499.cred", "copy_size", credential.copy_size, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.cred", "usage_value", credential.usage_value, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.cred", "caps_count", credential.caps_count, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.cred", "caps_value", credential.caps_value, 8),
            GLK_43499_PLAIN("backend.cve_2026_43499.cred", "ref0_image", credential.ref0_image, 8),
            GLK_43499_PLAIN("backend.cve_2026_43499.cred", "ref1_image", credential.ref1_image, 8),
            GLK_43499_PLAIN("backend.cve_2026_43499.cred", "ref2_image", credential.ref2_image, 8),
            GLK_43499_PLAIN("backend.cve_2026_43499.cred", "ref3_image", credential.ref3_image, 8),
            GLK_43499_PLAIN("backend.cve_2026_43499.offset", "slide_nfulnl_logger", offsets.slide_nfulnl_logger, 8),
            GLK_43499_PLAIN("backend.cve_2026_43499.offset", "slide_loggers_0_1", offsets.slide_loggers_0_1, 8),
            GLK_43499_PLAIN("backend.cve_2026_43499.offset", "slide_boot_id", offsets.slide_boot_id, 8),
            GLK_43499_PLAIN("backend.cve_2026_43499.offset", "vr_sys_exit_tp", misc.vr_sys_exit_tp, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.recommended_cpus", "main", execution.recommended_main_cpu, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.recommended_cpus", "consumer", execution.recommended_consumer_cpu, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.heap", "prepare_max_attempts", execution.heap_prepare_max_attempts, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.heap", "prepare_timeout_ms", execution.heap_prepare_timeout_ms, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.heap", "kernelsnitch_timeout_ms", execution.heap_kernelsnitch_timeout_ms, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.race", "route_wait_ms", execution.race_route_wait_ms, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.race", "route_done_timeout_ms", execution.race_route_done_timeout_ms, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.race", "setup_settle_us", execution.race_setup_settle_us, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.race", "state_poll_interval_us", execution.race_state_poll_interval_us, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.stages", "w1_attempts", execution.w1_attempts, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.stages", "w1_settle_us", execution.w1_settle_us, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.stages", "w1_scratch_repair_attempts", execution.w1_scratch_repair_attempts, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.stages", "w2_attempts", execution.w2_attempts, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.stages", "w2_settle_us", execution.w2_settle_us, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.stages", "w3_chain_rounds", execution.w3_chain_rounds, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.stages", "w3_attempts", execution.w3_attempts, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.stages", "w3_settle_us", execution.w3_settle_us, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.handoff", "pre_dispatch_settle_ms", execution.handoff_pre_dispatch_settle_ms, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.handoff", "module_poll_attempts", execution.handoff_module_poll_attempts, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.handoff", "module_poll_interval_ms", execution.handoff_module_poll_interval_ms, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.handoff", "enforce_poll_attempts", execution.handoff_enforce_poll_attempts, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.handoff", "enforce_poll_interval_ms", execution.handoff_enforce_poll_interval_ms, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.consumer", "max_calls", execution.select_consumer_max_calls, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.execution.consumer", "burst_calls", execution.select_consumer_burst_calls, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.route.tcp_zerocopy", "attempts", execution.tcp_attempts, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.route.tcp_zerocopy", "arm_sequence", execution.tcp_arm_sequence, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.route.tcp_zerocopy", "post_receive_hold_iterations", execution.tcp_post_receive_hold_iterations, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.route.select_stack", "enter_delay_us", execution.select_enter_delay_us, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.route.select_stack", "timeout_us", execution.select_timeout_us, 4),
            GLK_43499_PLAIN("backend.cve_2026_43499.route.multicast_waiter", "attempts", mcast_attempts, 1),
            GLK_43499_PLAIN("backend.cve_2026_43499.route.multicast_waiter", "arm_sequence", mcast_arm_sequence, 1),
            GLK_43499_PLAIN("backend.cve_2026_43499.route.multicast_waiter", "arm_hold", mcast_arm_hold, 2),
            GLK_43499_OPT("backend.cve_2026_43499.kernel", "compact_waiter", misc.compact_waiter, 1, false),
            GLK_43499_OPT("backend.cve_2026_43499.kernel", "kernelsnitch_collisions", misc.kernelsnitch_collisions, 4, false),
            GLK_43499_OPT("backend.cve_2026_43499.kernel", "mm_struct_sz", misc.mm_struct_sz, 4, false),
            GLK_43499_OPT("backend.cve_2026_43499.route.tcp_zerocopy", "payload_delta", geometry.tcp_payload_delta, 8, true),
            GLK_43499_OPT("backend.cve_2026_43499.route.tcp_zerocopy", "chunk_bias", geometry.tcp_chunk_bias, 8, false),
            GLK_43499_OPT("backend.cve_2026_43499.route.tcp_zerocopy", "fake_task_off", geometry.tcp_fake_task_off, 8, false),
            GLK_43499_OPT("backend.cve_2026_43499.route.tcp_zerocopy", "cred_copy_off", geometry.tcp_cred_copy_off, 8, false),
            GLK_43499_OPT("backend.cve_2026_43499.route.select_stack", "waiter_shift", geometry.pselect_waiter_shift, 4, true),
            GLK_43499_OPT("backend.cve_2026_43499.route.select_stack", "compact_waiter", misc.compact_waiter, 1, false),
            GLK_43499_OPT("backend.cve_2026_43499.route.multicast_waiter", "waiter_off", geometry.mcast_waiter_off, 4, true),
            GLK_43499_OPT("backend.cve_2026_43499.route.multicast_waiter", "buffer_size", geometry.mcast_buffer_size, 4, false),
            GLK_43499_OPT("backend.cve_2026_43499.route.multicast_waiter", "task_offset", geometry.mcast_task_offset, 4, false),
            GLK_43499_OPT("backend.cve_2026_43499.route.multicast_waiter", "lock_offset", geometry.mcast_lock_offset, 4, false),
            /* M2 queue selection (design doc 4.5/5.0): the canonical queue is an
             * array of map, the route is its queue-level sibling and experimental
             * is the U5 static opt-in. All three are selection-owned. */
            GLK_43499_SELECTION("backend.cve_2026_43499", "queue",
                                profile::WireKind::Array, 0,
                                "Step queue: array of {step|seam[,stage]} (M2)."),
            GLK_43499_SELECTION("backend.cve_2026_43499", "route",
                                profile::WireKind::String, 0,
                                "Queue-level route token (M2; required here)."),
            GLK_43499_SELECTION("backend.cve_2026_43499", "experimental",
                                profile::WireKind::Bool, 1,
                                "Static experimental opt-in declaration (U5)."),
            GLK_43499_STEPS("backend.cve_2026_43499", "steps"),
        };
    };

#undef GLK_43499_PLAIN
#undef GLK_43499_OPT
#undef GLK_43499_STEPS
#undef GLK_43499_SELECTION

} // namespace ghostlock::backend

/* S4 R1 registry composition: both catalogued 43499 step sets share the platform
 * ABI owner and the 43499 backend owner. The route is backend-internal and is
 * not part of ComponentSelection, so these two triples cover the catalogue. */
namespace ghostlock::profile {
    template<>
    struct RegistryForSelection<contract::ComponentSelection{
            contract::BackendKind::Cve2026_43499,
            contract::StepSetKind::W1W2,
            contract::TerminalKind::RootChild}> {
        using type = SchemaRegistry<platform::abi::Schema,
                                    backend::Cve2026_43499Schema>;
    };

    template<>
    struct RegistryForSelection<contract::ComponentSelection{
            contract::BackendKind::Cve2026_43499,
            contract::StepSetKind::W1W3,
            contract::TerminalKind::RootChild}> {
        using type = SchemaRegistry<platform::abi::Schema,
                                    backend::Cve2026_43499Schema>;
    };
} // namespace ghostlock::profile

#endif
