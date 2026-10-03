#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_SCHEMA_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_SCHEMA_HPP

/* Owner schema for the cve_2026_43284 private profile section (S3 B4).
 *
 * The section name is `backend.cve_2026_43284`, sibling-in-kind to
 * `backend.cve_2026_43499.steps` but a distinct owner: 43284 never reuses a
 * 43499 wire slot. Every field is optional (presence is expressed by key
 * occurrence), and each carries a numeric policy value rather than literal
 * text because the GLK1 v2 value is a single 64-bit slot (ADR-0003 decision 1).
 * The 43284 backend (B5) owns the token -> string resolution. */

#include "backend/cve_2026_43284_state.hpp"
#include "profile/schema.hpp"

#include <cstdint>
#include <string_view>

namespace ghostlock::backend {
    using Cve2026_43284Field = profile::FieldSpec<Cve2026_43284Profile>;

    inline constexpr std::string_view kCve2026_43284Section =
            "backend.cve_2026_43284";

#define GLK_43284_OPT_U64(key, member)                                           \
    {                                                                            \
        kCve2026_43284Section, key, 8, false, false,                             \
                [](Cve2026_43284Profile &view, uint64_t raw) {                   \
                    view.member = raw;                                           \
                }                                                                \
    }
#define GLK_43284_OPT_U16(key, member)                                           \
    {                                                                            \
        kCve2026_43284Section, key, 2, false, false,                             \
                [](Cve2026_43284Profile &view, uint64_t raw) {                   \
                    view.member = static_cast<uint16_t>(raw);                    \
                }                                                                \
    }

    struct Cve2026_43284Schema final {
        using View = Cve2026_43284Profile;

        static constexpr Cve2026_43284Field kFields[] = {
            GLK_43284_OPT_U64("carrier_path", carrier_path),
            GLK_43284_OPT_U64("lkm_path", lkm_path),
            GLK_43284_OPT_U16("kmi", kmi),
            GLK_43284_OPT_U64("selinux_exec_context", selinux_exec_context),
            GLK_43284_OPT_U64("late_load_args", late_load_args),
            GLK_43284_OPT_U64("defex_symbol", defex_symbol),
            GLK_43284_OPT_U16("steps", steps),
        };
    };

#undef GLK_43284_OPT_U64
#undef GLK_43284_OPT_U16

    static_assert(profile::SchemaDefinition<Cve2026_43284Schema>);
} // namespace ghostlock::backend

#endif
