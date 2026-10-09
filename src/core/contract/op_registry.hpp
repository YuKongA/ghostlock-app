#pragma once
/** @file contract/op_registry.hpp
 *  P4 step 4: the op registry -- the PLAN side's op vocabulary and parameter
 *  bounds, consumed by pipeline::plan_gate and by nothing else. This table is NOT
 *  the script's visible face: a script reaches only the seven names contract::kSurface
 *  declares (user-approved, A6 baseline item 3), and the names in this table --
 *  window.* above all -- are exactly the ones a script may never spell (the frozen
 *  criterion surface_has_no_window_prefixed_names plus the grep in make -C src
 *  check). A script's calls are recorded as staged surface entries and fall to
 *  these ops on the plan side; there is no op(...) entry point in the sandbox.
 *
 *  Authority: docs/analysis/composition-layer-p4-spec.md section 2.
 *
 *  Rules this header implements:
 *   - one constexpr table, no runtime registration;
 *   - compile-time uniqueness of tokens and of the table's shape;
 *   - an op token resolves here and nowhere else: the tokens below appear exactly
 *     once in src/core (checked by the gate's grep). The reader is the plan side,
 *     not the script: see the note above on which table a script may reach;
 *   - ParamSpec.max is the ONLY upper bound for a window parameter. The gate
 *     never invents a bound, and never hardcodes which parameter is required:
 *     it reads the flag from the table (R10 may change the flags, not the code).
 *
 *  Supersedes contract::StepExecution (the M2 per-step registration point) as
 *  the place a new execution unit is declared. The frozen P2 step catalogue
 *  stays untouched and remains the compatibility face.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

// A0 adjustment (Q1): the route vocabulary now lives in the new tree as
// profile/route_catalog.hpp; everything else in this file is unchanged.
#include "profile/route_catalog.hpp"

namespace ghostlock::contract {
    /** Wire kind of one parameter (mirrors the GLKv3 scalar kinds). */
    enum class ParamWire : unsigned char { Uint = 0, Int = 1, Bool = 2, Str = 3 };

    /** One parameter of one op. */
    struct ParamSpec final {
        const char *key;
        ParamWire wire;
        /** Required-ness is DATA: the gate reads this flag, never a name list. */
        bool required;
        /** Sole upper bound for window parameters (inclusive). */
        std::uint64_t max;
    };

    /** Bit i of a route mask names kRouteCatalog[i] (single route authority). */
    using RouteMask = std::uint32_t;

    /** Bit for a catalogue route token; an unknown token yields 0 (fail-closed). */
    [[nodiscard]] constexpr RouteMask route_bit(std::string_view token) noexcept {
        for (std::size_t i = 0; i < std::size(profile::kRouteCatalog); ++i) {
            if (token == profile::kRouteCatalog[i].token) {
                return static_cast<RouteMask>(1U) << i;
            }
        }
        return 0U;
    }

    /** Routes this build can actually drive (mirrors the wired combinations). */
    inline constexpr RouteMask kAvailableRouteMask =
        route_bit("tcp_zerocopy") | route_bit("select_stack") | route_bit("multicast_waiter");

    /** One op: the only handle a script gets on the attack machinery. */
    struct OpSpec final {
        const char *token;
        RouteMask routes;
        /** Window atomicity: the whole window is ONE op (spec section 4.1). */
        bool runs_in_window;
        /** Available in this build (an unavailable op is rejected by the gate). */
        bool available;
        const ParamSpec *params;
        std::size_t param_count;
        /** Capability nouns the op needs (facts come from contract::Capabilities). */
        const char *requires_caps;
        const char *effect;
    };

    /* ---- window parameter tables (bounds are data, never code) ---- */
    inline constexpr ParamSpec kWindowW1W3Params[] = {
        {"attempts", ParamWire::Uint, true, 8U},
        {"nice", ParamWire::Int, false, 19U},
        {"preserve_child", ParamWire::Bool, false, 1U},
    };
    inline constexpr ParamSpec kWindowW1W2Params[] = {
        {"attempts", ParamWire::Uint, false, 8U},
        {"nice", ParamWire::Int, false, 19U},
    };
    inline constexpr ParamSpec kPagecacheParams[] = {
        {"burst_calls", ParamWire::Uint, false, 64U},
        {"settle_us", ParamWire::Uint, false, 200000U},
    };

    /** The registry table: the script token vocabulary, declared once. */
    inline constexpr OpSpec kOpCatalog[] = {
        {"window.w1w3", kAvailableRouteMask, true, true, kWindowW1W3Params,
         std::size(kWindowW1W3Params), "selinux+cred+seccomp", "root child handed off"},
        {"window.w1w2", kAvailableRouteMask, true, true, kWindowW1W2Params,
         std::size(kWindowW1W2Params), "selinux+cred", "uid 0 in the session"},
        {"window.pagecache", route_bit("select_stack") | route_bit("tcp_zerocopy"), false, true,
         kPagecacheParams, std::size(kPagecacheParams), "file_cache_write", "patched bearer"},
        /* Declared but not wired yet (mirrors the catalogue's available=false
         * combinations): the gate must refuse it by name, never silently run it. */
        {"window.w1w3_umh", kAvailableRouteMask, true, false, kWindowW1W3Params,
         std::size(kWindowW1W3Params), "umh_forward", "planned, not wired"},
    };
    inline constexpr std::size_t kOpCount = std::size(kOpCatalog);

    /** The one table a script token may resolve against. */
    struct OpRegistry final {
        const OpSpec *ops;
        std::size_t count;
    };
    inline constexpr OpRegistry kOpRegistry{kOpCatalog, kOpCount};

    /* ---- compile-time guarantees (spec section 2) ---- */
    /** Same-text helper (constexpr string comparison). */
    [[nodiscard]] constexpr bool op_same_text(const char *a, const char *b) noexcept {
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        return *a == *b;
    }

    /** Tokens are unique: a second entry for one token is a compile error. */
    [[nodiscard]] constexpr bool op_tokens_unique() noexcept {
        for (std::size_t i = 0; i < kOpCount; ++i) {
            if (kOpCatalog[i].token == nullptr || kOpCatalog[i].token[0] == '\0') {
                return false;
            }
            for (std::size_t j = i + 1; j < kOpCount; ++j) {
                if (op_same_text(kOpCatalog[i].token, kOpCatalog[j].token)) {
                    return false;
                }
            }
        }
        return true;
    }

    /** Every op's routes must be inside the available mask. */
    [[nodiscard]] constexpr bool op_routes_are_available() noexcept {
        for (std::size_t i = 0; i < kOpCount; ++i) {
            if ((kOpCatalog[i].routes & ~kAvailableRouteMask) != 0U) {
                return false;
            }
        }
        return true;
    }

    /** Parameter keys inside one op are unique and their bounds are non-zero. */
    [[nodiscard]] constexpr bool op_params_are_well_formed() noexcept {
        for (std::size_t i = 0; i < kOpCount; ++i) {
            const OpSpec &op = kOpCatalog[i];
            for (std::size_t j = 0; j < op.param_count; ++j) {
                if (op.params[j].key == nullptr || op.params[j].key[0] == '\0') {
                    return false;
                }
                if (op.params[j].wire != ParamWire::Bool && op.params[j].max == 0U) {
                    return false;
                }
                for (std::size_t k = j + 1; k < op.param_count; ++k) {
                    if (op_same_text(op.params[j].key, op.params[k].key)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    /** Look up an op by token; nullptr means the token is not in the registry. */
    [[nodiscard]] inline const OpSpec *op_by_token(std::string_view token) noexcept {
        for (const OpSpec &op : kOpCatalog) {
            if (token == op.token) {
                return &op;
            }
        }
        return nullptr;
    }

    /** Stable token hash (FNV-1a): the gate compares it with the plan's claim. */
    [[nodiscard]] constexpr std::uint64_t op_token_hash(std::string_view token) noexcept {
        std::uint64_t h = 1469598103934665603ULL;
        for (const char c : token) {
            h ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
            h *= 1099511628211ULL;
        }
        return h;
    }

    static_assert(kOpCount >= 1, "the op registry must not be empty");
    static_assert(op_tokens_unique(), "op tokens must be unique");
    static_assert(op_routes_are_available(), "op routes must be inside the available mask");
    static_assert(op_params_are_well_formed(), "op parameters must be unique and bounded");
    static_assert(kAvailableRouteMask != 0U, "at least one route must be available");
}  // namespace ghostlock::contract
