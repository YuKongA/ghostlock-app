#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_GLKV3_SCHEMA_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_GLKV3_SCHEMA_HPP

/* GLKv3 (MessagePack) path -> type declaration for the cve_2026_43284 owner
 * (GLKv3-3, ADR-0003 wire migration).
 *
 * kCve2026_43284Glkv3Fields mirrors schema.hpp's owner Schema one-for-one:
 * the key names are byte-for-byte identical and the (section,key) set is equal
 * (asserted by glkv3_schema_test). S4 R4 stringifies the policy paths
 * (carrier_path / lkm_path -> WireType::Str, <=256 UTF-8 bytes: the two
 * runtime-injected keys) and adds the handshake/wait tuning
 * (wait_timeout_ms / module_poll_attempts / module_poll_interval_ms -> UInt).
 * All fields are optional: presence is expressed by key occurrence, matching the
 * owner Schema.
 *
 * HOCON refactor layout:
 *   - backend.cve_2026_43284.steps      -> the selection token (selection axis);
 *   - backend.cve_2026_43284.execution.* -> the five execution knobs
 *     (late_load_args / selinux_exec_context / module_poll_attempts /
 *      module_poll_interval_ms / wait_timeout_ms); the wire carries the
 *     execution. level too, never a flattened key;
 *   - backend.cve_2026_43284.{kmi,lkm_path,carrier_path} -> wire_only: the wire
 *     keeps them, the profile does not declare them (the manifest omits them),
 *     and native resolves each at the point of use (kmi = major*1000 + minor
 *     from the release, lkm_path = $GHOSTLOCK_HOME/helper.ko, carrier_path =
 *     first device-present candidate). */

#include "profile/glkv3.hpp"

namespace ghostlock::backend {
    inline constexpr profile::glkv3::FieldSpec kCve2026_43284Glkv3Fields[] = {
        /* Backend top level: selection token + the three runtime-injected,
         * wire-only keys (no profile declaration; see the header). */
        /* M2 queue selection: see the 43499 table (each row has a matching v2
         * selection-owned declaration). */
        {"backend.cve_2026_43284", "queue", profile::glkv3::WireType::Array, false},
        {"backend.cve_2026_43284", "route", profile::glkv3::WireType::Str, false},
        {"backend.cve_2026_43284", "experimental", profile::glkv3::WireType::Bool, false},
        {"backend.cve_2026_43284", "steps", profile::glkv3::WireType::Str, false},
        {"backend.cve_2026_43284", "kmi", profile::glkv3::WireType::UInt, false, true},
        {"backend.cve_2026_43284", "lkm_path", profile::glkv3::WireType::Str, false, true},
        {"backend.cve_2026_43284", "carrier_path", profile::glkv3::WireType::Str, false, true},
        /* Execution tuning: profile-declarable, one flat section. */
        {"backend.cve_2026_43284.execution", "late_load_args", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284.execution", "selinux_exec_context", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284.execution", "wait_timeout_ms", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284.execution", "module_poll_attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284.execution", "module_poll_interval_ms", profile::glkv3::WireType::UInt, false},
    };

    inline constexpr profile::glkv3::Schema kCve2026_43284Glkv3Schema{kCve2026_43284Glkv3Fields};
} // namespace ghostlock::backend

#endif
