#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_GLKV3_SCHEMA_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_GLKV3_SCHEMA_HPP

/* GLKv3 (MessagePack) path -> type declaration for the cve_2026_43284 owner
 * (GLKv3-3, ADR-0003 wire migration).
 *
 * kCve2026_43284Glkv3Fields mirrors schema.hpp's owner Schema one-for-one:
 * the key names are byte-for-byte identical and the (section,key) set is equal
 * (asserted by glkv3_schema_test). S4 R4 stringifies the three policy paths
 * (carrier_path / lkm_path / defex_symbol -> WireType::Str, <=256 UTF-8 bytes)
 * and adds the handshake/wait tuning (wait_timeout_ms / module_poll_attempts /
 * module_poll_interval_ms -> UInt). All fields are optional: presence is
 * expressed by key occurrence, matching the owner Schema. */

#include "profile/glkv3.hpp"

namespace ghostlock::backend {
    inline constexpr profile::glkv3::FieldSpec kCve2026_43284Glkv3Fields[] = {
        {"backend.cve_2026_43284", "carrier_path", profile::glkv3::WireType::Str, false},
        {"backend.cve_2026_43284", "lkm_path", profile::glkv3::WireType::Str, false},
        {"backend.cve_2026_43284", "defex_symbol", profile::glkv3::WireType::Str, false},
        {"backend.cve_2026_43284", "kmi", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284", "selinux_exec_context", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284", "late_load_args", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284", "steps", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284", "wait_timeout_ms", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284", "module_poll_attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43284", "module_poll_interval_ms", profile::glkv3::WireType::UInt, false},
    };

    inline constexpr profile::glkv3::Schema kCve2026_43284Glkv3Schema{kCve2026_43284Glkv3Fields};
} // namespace ghostlock::backend

#endif
