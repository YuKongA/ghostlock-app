#pragma once
/** @file contract/script_surface.hpp
 *  The script exposure surface: which names a script may reach, what each name
 *  is, and which layer implements it. MECHANISM ONLY -- this file carries no
 *  sandbox semantics, no loader and no Lua reference (the sandbox batch wires
 *  it); it is the single place the surface names are spelled.
 *
 *  Placement: contract/ (not script/) because the table is neutral vocabulary
 *  that the script layer, the loader and the tests all read; script/ will own
 *  the enforcement, and putting the table there would make the enforcement the
 *  authority over the vocabulary it must obey.
 *
 *  Fields per row: name; form (Call/Value); args (keyed specs whose max is the
 *  SCHEMA acceptance ceiling); implements_layer (must name a layer of
 *  contract::kLayers); recordable; executable; window_allowed; holds_resource
 *  (keeps a resource alive beyond the call -- handle territory, and ADR-0010 is
 *  still Proposed, so nothing here implements it).
 *
 *  Bound relationship (numbers policy): the max below is a schema ceiling. The
 *  EFFECTIVE bound is the profile's (P4 contract/op_registry.hpp ParamSpec.max);
 *  a profile value above the ceiling is a named rejection (param-invalid) and
 *  no default is ever substituted. Ceilings are acceptance limits, not tuning.
 *
 *  Language-level constructs (loops, conditionals) are NOT rows: they belong to
 *  the interpreter, not to the exposed capability surface.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "contract/layer_scope.hpp"

namespace ghostlock::contract {
    /** How a surface name is used. */
    enum class SurfaceForm : unsigned char {
        Call = 0,
        Value = 1,
    };

    /** One keyed argument; max is the schema acceptance ceiling. */
    struct ArgSpec final {
        const char *key;
        std::uint64_t max;
    };

    /** One row of the exposure surface. */
    struct SurfaceEntry final {
        const char *name;
        SurfaceForm form;
        const ArgSpec *args;
        std::size_t arg_count;
        const char *implements_layer;
        bool recordable;
        bool executable;
        bool window_allowed;
        bool holds_resource;
    };

    inline constexpr ArgSpec kWriteOpArgs[] = {
        {"attempts", 64U},
        {"preserve_child", 1U},
    };
    inline constexpr ArgSpec kSettleArgs[] = {
        {"settle_us", 1000000U},
    };
    inline constexpr ArgSpec kProfileGetArgs[] = {
        {"key", 256U},
    };

    /** The surface table (draft: backend / writeop / settle / handoff /
     *  profile.get). Loops and conditionals deliberately have no row. */
    inline constexpr SurfaceEntry kSurface[] = {
        {"backend", SurfaceForm::Call, nullptr, 0, "backend", false, false, false, false},
        {"writeop", SurfaceForm::Call, kWriteOpArgs, sizeof(kWriteOpArgs) / sizeof(kWriteOpArgs[0]), "backend",
         true, true, true, false},
        {"settle", SurfaceForm::Call, kSettleArgs, sizeof(kSettleArgs) / sizeof(kSettleArgs[0]), "race", true,
         true, true, false},
        {"handoff", SurfaceForm::Call, nullptr, 0, "terminal", true, true, false, true},
        {"profile.get", SurfaceForm::Call, kProfileGetArgs,
         sizeof(kProfileGetArgs) / sizeof(kProfileGetArgs[0]), "profile", false, false, false, false},
    };
    inline constexpr std::size_t kSurfaceCount = sizeof(kSurface) / sizeof(kSurface[0]);

    /** Lookup outcome: found, or the named rejection unknown-symbol. */
    enum class SurfaceLookup : unsigned char {
        Found = 0,
        UnknownSymbol = 1,
    };

    [[nodiscard]] constexpr const char *surface_lookup_name(SurfaceLookup code) noexcept {
        switch (code) {
            case SurfaceLookup::Found: return "found";
            case SurfaceLookup::UnknownSymbol: return "unknown-symbol";
        }
        return "unknown";
    }

    struct SurfaceResult final {
        SurfaceLookup code = SurfaceLookup::UnknownSymbol;
        const SurfaceEntry *entry = nullptr;
    };

    /** The only way to reach a row: by name, with a named refusal otherwise. */
    [[nodiscard]] constexpr SurfaceResult lookup(std::string_view name) noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            if (name == kSurface[i].name) {
                return SurfaceResult{SurfaceLookup::Found, &kSurface[i]};
            }
        }
        return SurfaceResult{SurfaceLookup::UnknownSymbol, nullptr};
    }

    /* ---- compile-time guarantees (each one can fail) ---- */
    [[nodiscard]] constexpr bool surface_same_text(const char *a, const char *b) noexcept {
        while (*a != '\0' && *a == *b) {
            ++a;
            ++b;
        }
        return *a == *b;
    }

    [[nodiscard]] constexpr bool surface_names_are_unique_and_non_empty() noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            if (kSurface[i].name == nullptr || kSurface[i].name[0] == '\0') {
                return false;
            }
            for (std::size_t j = i + 1; j < kSurfaceCount; ++j) {
                if (surface_same_text(kSurface[i].name, kSurface[j].name)) {
                    return false;
                }
            }
        }
        return true;
    }

    /** Every row must name a layer that the layer catalog declares. */
    [[nodiscard]] constexpr bool surface_layers_exist() noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            bool known = false;
            for (std::size_t k = 0; k < kLayerCount; ++k) {
                if (surface_same_text(kSurface[i].implements_layer, kLayers[k].name)) {
                    known = true;
                }
            }
            if (!known) {
                return false;
            }
        }
        return true;
    }

    /** Argument specs: keys unique inside a row and ceilings non-zero. */
    [[nodiscard]] constexpr bool surface_args_are_well_formed() noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            const SurfaceEntry &row = kSurface[i];
            if (row.arg_count != 0 && row.args == nullptr) {
                return false;
            }
            for (std::size_t a = 0; a < row.arg_count; ++a) {
                if (row.args[a].key == nullptr || row.args[a].key[0] == '\0' || row.args[a].max == 0U) {
                    return false;
                }
                for (std::size_t b = a + 1; b < row.arg_count; ++b) {
                    if (surface_same_text(row.args[a].key, row.args[b].key)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    /** A window-reachable row must be executable there; a row that holds a
     *  resource must be recordable (it outlives its call). */
    [[nodiscard]] constexpr bool surface_flags_are_consistent() noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            if (kSurface[i].window_allowed && !kSurface[i].executable) {
                return false;
            }
            if (kSurface[i].holds_resource && !kSurface[i].recordable) {
                return false;
            }
        }
        return true;
    }

    static_assert(kSurfaceCount >= 1, "the exposure surface must not be empty");
    static_assert(surface_names_are_unique_and_non_empty(), "surface names must be unique and non-empty");
    static_assert(surface_layers_exist(), "every surface row must name a declared layer");
    static_assert(surface_args_are_well_formed(), "argument specs need unique keys and non-zero ceilings");
    static_assert(surface_flags_are_consistent(), "window/holds_resource flags must stay consistent");
}  // namespace ghostlock::contract
