#pragma once
/** @file contract/script_surface.hpp
 *  The script exposure surface: which names a script may reach, what each name
 *  is, and which layer implements it. MECHANISM ONLY -- this file carries no
 *  sandbox semantics, no loader, no Lua reference and NO HANDLE TABLE (ADR-0010
 *  is still Proposed; the handle table itself belongs to the A6 sandbox batch).
 *  It is the single place the surface names are spelled.
 *
 *  Placement: contract/ (not script/) because the table is neutral vocabulary
 *  that the script layer, the loader and the tests all read; script/ will own
 *  the enforcement, and putting the table there would make the enforcement the
 *  authority over the vocabulary it must obey.
 *
 *  Fields per row: name; form (Call = an action reachable from the global
 *  surface, Value = a read-only snapshot, HandleMethod = a method invoked ON a
 *  handle); args (keyed specs: max is the SCHEMA acceptance ceiling, required
 *  says whether a script must pass it); implements_layer (must name a layer of
 *  contract::kLayers); recordable; executable; window_allowed; holds_resource
 *  (keeps a resource alive beyond the call).
 *
 *  WINDOW-LEVEL OPS ARE NOT SCRIPT-VISIBLE ENTRIES (user ruling): the names
 *  spelled window.* are a RUNTIME-INTERNAL PHASE GROUPING, not a script
 *  capability; the exposure table therefore contains no row whose name starts
 *  with "window.". Two machine criteria enforce it: the constexpr assertion
 *  surface_has_no_window_prefixed_names() below, and the grep performed by
 *  "make -C src check".
 *
 *  NO "run" ROW (user ruling): the shell/argv entry was removed from the
 *  surface, together with the run-form-invalid vocabulary. A named assertion
 *  (surface_has_no_run_row below) keeps it from creeping back.
 *
 *  WRITE TARGETS ARE TAGGED (user ruling): a write target is the pair
 *  {target_kind, target_value}. The kind is a CLOSED mechanism set declared once
 *  here (kernel_offset / absolute / direct_map) and the value's effective bound
 *  comes from the profile (schema ceiling = the whole 64-bit domain). The pair is
 *  modelled as TWO flat ArgSpec keys rather than as a record parameter, because
 *  the flat table is what the lookup path and the loader already consume; a
 *  record shape would add a second table machinery that the sandbox batch does
 *  not need yet. A compile-time assertion keeps the two keys together.
 *
 *  Language-level constructs (loops, conditionals, snapshot field access) are
 *  NOT rows: they belong to the interpreter, not to the exposed surface.
 *
 *  Bound relationship (numbers policy): the max below is a schema ceiling. The
 *  EFFECTIVE bound is the profile's (P4 contract/op_registry.hpp ParamSpec.max);
 *  a profile value above the ceiling is a named rejection (param-invalid) and no
 *  default is ever substituted. Ceilings are acceptance limits, not tuning.
 *  Values never carry kernel offsets: a write names its target by KIND plus the
 *  profile-supplied value, so no raw offset appears in a script.
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
        HandleMethod = 2,
    };

    /** One keyed argument; max = schema acceptance ceiling, required = must the
     *  script pass it. */
    struct ArgSpec final {
        const char *key;
        std::uint64_t max;
        bool required;
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

    /* The whole 64-bit domain: a value whose real bound is the profile's, and a
     * handle word (slot + generation), are accepted at the schema level. */
    inline constexpr std::uint64_t kValueCeiling = ~static_cast<std::uint64_t>(0);

    /* ---- write target kinds: the closed mechanism set ---- */
    enum class TargetKind : unsigned char {
        KernelOffset = 0,
        Absolute = 1,
        DirectMap = 2,
    };

    /** The kind vocabulary, spelled once. */
    inline constexpr const char *kTargetKindNames[] = {
        "kernel_offset",
        "absolute",
        "direct_map",
    };
    inline constexpr std::size_t kTargetKindCount = sizeof(kTargetKindNames) / sizeof(kTargetKindNames[0]);

    [[nodiscard]] constexpr const char *target_kind_name(TargetKind kind) noexcept {
        switch (kind) {
            case TargetKind::KernelOffset: return kTargetKindNames[0];
            case TargetKind::Absolute: return kTargetKindNames[1];
            case TargetKind::DirectMap: return kTargetKindNames[2];
        }
        return "unknown";
    }

    /** Which refusal -- if any -- a target kind name earns. Mechanism only. */
    enum class TargetKindCheck : unsigned char {
        Valid = 0,
        Unknown = 1,
    };
    inline constexpr const char *kTargetKindInvalidReason = "target-kind-invalid";

    [[nodiscard]] constexpr const char *target_kind_check_name(TargetKindCheck check) noexcept {
        switch (check) {
            case TargetKindCheck::Valid: return "valid";
            case TargetKindCheck::Unknown: return kTargetKindInvalidReason;
        }
        return "unknown";
    }

    [[nodiscard]] constexpr TargetKindCheck target_kind_from_name(std::string_view name) noexcept {
        for (std::size_t i = 0; i < kTargetKindCount; ++i) {
            if (name == kTargetKindNames[i]) {
                return TargetKindCheck::Valid;
            }
        }
        return TargetKindCheck::Unknown;
    }

    /* ---- handle rejections: mechanism skeleton only (no handle table) ---- */
    inline constexpr const char *kHandleConflictReason = "handle-conflict";
    inline constexpr const char *kHandleStaleReason = "handle-stale";

    /** The thin check A6 will drive. slot_live = the slot still owns a resource;
     *  generations must match, and opening onto a live slot is a conflict. */
    enum class HandleCheck : unsigned char {
        Valid = 0,
        Conflict = 1,
        Stale = 2,
    };

    [[nodiscard]] constexpr const char *handle_check_name(HandleCheck check) noexcept {
        switch (check) {
            case HandleCheck::Valid: return "valid";
            case HandleCheck::Conflict: return kHandleConflictReason;
            case HandleCheck::Stale: return kHandleStaleReason;
        }
        return "unknown";
    }

    /** Using a handle: stale when the slot is gone or the generation moved. */
    [[nodiscard]] constexpr HandleCheck handle_use_check(bool slot_live, std::uint64_t slot_generation,
                                                         std::uint64_t handle_generation) noexcept {
        if (!slot_live || slot_generation != handle_generation) {
            return HandleCheck::Stale;
        }
        return HandleCheck::Valid;
    }

    /** Opening a handle: a live slot refuses a second owner. */
    [[nodiscard]] constexpr HandleCheck handle_open_check(bool slot_live) noexcept {
        return slot_live ? HandleCheck::Conflict : HandleCheck::Valid;
    }

    /* ---- argument tables (schema ceilings; see the file comment) ---- */
    inline constexpr ArgSpec kWriteOpArgs[] = {
        {"target_kind", 64U, true},          /* TargetKind name, never an offset */
        {"target_value", kValueCeiling, true}, /* profile-bounded */
        {"value", kValueCeiling, true},
        {"width", 8U, false},
    };
    inline constexpr ArgSpec kCopyOpArgs[] = {
        {"src_kind", 64U, true},
        {"src_value", kValueCeiling, true},
        {"dst_kind", 64U, true},
        {"dst_value", kValueCeiling, true},
        {"width", 8U, true},
    };
    inline constexpr ArgSpec kSettleArgs[] = {
        {"us", 1000000U, true},
    };
    inline constexpr ArgSpec kHandoffArgs[] = {
        {"terminal", 256U, true},
    };
    inline constexpr ArgSpec kCloseArgs[] = {
        {"handle", kValueCeiling, true}, /* slot + generation word */
    };

    /** The surface table: backend / writeop / copyop / settle / handoff / cfg /
     *  close. profile.get was removed (cfg replaces it), run was removed, and no
     *  window.* row may appear (see the file comment). */
    inline constexpr SurfaceEntry kSurface[] = {
        {"backend", SurfaceForm::Call, nullptr, 0, "backend", false, false, false, false},
        {"writeop", SurfaceForm::Call, kWriteOpArgs, sizeof(kWriteOpArgs) / sizeof(kWriteOpArgs[0]), "backend",
         true, true, true, false},
        {"copyop", SurfaceForm::Call, kCopyOpArgs, sizeof(kCopyOpArgs) / sizeof(kCopyOpArgs[0]), "backend", true,
         true, true, false},
        {"settle", SurfaceForm::Call, kSettleArgs, sizeof(kSettleArgs) / sizeof(kSettleArgs[0]), "race", true,
         true, true, false},
        {"handoff", SurfaceForm::Call, kHandoffArgs, sizeof(kHandoffArgs) / sizeof(kHandoffArgs[0]), "terminal",
         true, true, false, true},
        {"cfg", SurfaceForm::Value, nullptr, 0, "profile", true, false, false, false},
        {"close", SurfaceForm::HandleMethod, kCloseArgs, sizeof(kCloseArgs) / sizeof(kCloseArgs[0]), "session",
         true, true, false, true},
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

    /** True when the name starts with the runtime-internal phase prefix. */
    [[nodiscard]] constexpr bool surface_is_window_prefixed(const char *name) noexcept {
        constexpr const char *prefix = "window.";
        std::size_t i = 0;
        while (prefix[i] != '\0') {
            if (name == nullptr || name[i] != prefix[i]) {
                return false;
            }
            ++i;
        }
        return true;
    }

    [[nodiscard]] constexpr bool surface_has_no_window_prefixed_names() noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            if (surface_is_window_prefixed(kSurface[i].name)) {
                return false;
            }
        }
        return true;
    }

    /** The removed shell entry must not come back. */
    [[nodiscard]] constexpr bool surface_has_no_run_row() noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            if (surface_same_text(kSurface[i].name, "run")) {
                return false;
            }
        }
        return true;
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

    /** True when the row declares the named argument. */
    [[nodiscard]] constexpr bool surface_row_has_arg(const SurfaceEntry &row, const char *key) noexcept {
        for (std::size_t a = 0; a < row.arg_count; ++a) {
            if (surface_same_text(row.args[a].key, key)) {
                return true;
            }
        }
        return false;
    }

    /** The write target is the PAIR {target_kind, target_value}: both or neither. */
    [[nodiscard]] constexpr bool writeop_target_is_a_pair() noexcept {
        const SurfaceResult result = lookup("writeop");
        if (result.code != SurfaceLookup::Found || result.entry == nullptr) {
            return false;
        }
        const bool kind = surface_row_has_arg(*result.entry, "target_kind");
        const bool value = surface_row_has_arg(*result.entry, "target_value");
        return kind && value;
    }

    /** copyop carries the same tagged pair on BOTH sides, plus a width. */
    [[nodiscard]] constexpr bool copyop_sides_are_symmetric() noexcept {
        const SurfaceResult result = lookup("copyop");
        if (result.code != SurfaceLookup::Found || result.entry == nullptr) {
            return false;
        }
        const SurfaceEntry &row = *result.entry;
        return surface_row_has_arg(row, "src_kind") && surface_row_has_arg(row, "src_value") &&
               surface_row_has_arg(row, "dst_kind") && surface_row_has_arg(row, "dst_value") &&
               surface_row_has_arg(row, "width");
    }

    /** A handle method must take the handle it acts on, and it is not a window
     *  op (closing a handle is not part of the racing phase). */
    [[nodiscard]] constexpr bool handle_methods_are_well_formed() noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            if (kSurface[i].form != SurfaceForm::HandleMethod) {
                continue;
            }
            if (!surface_row_has_arg(kSurface[i], "handle")) {
                return false;
            }
            if (kSurface[i].window_allowed) {
                return false;
            }
        }
        return true;
    }

    /** The target-kind vocabulary is closed: three names, unique, non-empty. */
    [[nodiscard]] constexpr bool target_kind_names_are_closed() noexcept {
        if (kTargetKindCount != 3) {
            return false;
        }
        for (std::size_t i = 0; i < kTargetKindCount; ++i) {
            if (kTargetKindNames[i] == nullptr || kTargetKindNames[i][0] == '\0') {
                return false;
            }
            for (std::size_t j = i + 1; j < kTargetKindCount; ++j) {
                if (surface_same_text(kTargetKindNames[i], kTargetKindNames[j])) {
                    return false;
                }
            }
        }
        return true;
    }

    /** Flags and form must agree: a window-reachable row must be executable
     *  there, a resource-holding row must be recordable, and a Value row is a
     *  read-only snapshot -- it is never executable. */
    [[nodiscard]] constexpr bool surface_flags_are_consistent() noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            if (kSurface[i].window_allowed && !kSurface[i].executable) {
                return false;
            }
            if (kSurface[i].holds_resource && !kSurface[i].recordable) {
                return false;
            }
            if (kSurface[i].form == SurfaceForm::Value && kSurface[i].executable) {
                return false;
            }
        }
        return true;
    }

    static_assert(kSurfaceCount >= 1, "the exposure surface must not be empty");
    static_assert(surface_names_are_unique_and_non_empty(), "surface names must be unique and non-empty");
    static_assert(surface_has_no_window_prefixed_names(),
                  "window.* is a runtime-internal phase grouping, never a script surface entry");
    static_assert(surface_has_no_run_row(), "the shell entry was removed: no run row may reappear");
    static_assert(surface_layers_exist(), "every surface row must name a declared layer");
    static_assert(surface_args_are_well_formed(), "argument specs need unique keys and non-zero ceilings");
    static_assert(writeop_target_is_a_pair(), "writeop targets are the tagged pair {target_kind, target_value}");
    static_assert(copyop_sides_are_symmetric(), "copyop needs the tagged pair on both sides plus a width");
    static_assert(handle_methods_are_well_formed(), "a handle method takes a handle and is not a window op");
    static_assert(target_kind_names_are_closed(), "the target-kind set is closed at three names");
    static_assert(surface_flags_are_consistent(),
                  "flags must stay consistent (window/holds_resource/Value-is-read-only)");
}  // namespace ghostlock::contract
