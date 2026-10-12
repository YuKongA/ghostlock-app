#pragma once
/** @file contract/script_surface.hpp
 *  The script exposure surface: which names a script may reach, what each name
 *  is, and which layer implements it. MECHANISM ONLY -- this file carries no
 *  sandbox semantics, no loader, no Lua reference and NO HANDLE TABLE (ADR-0010
 *  is still Proposed; the handle table itself belongs to the A6 sandbox batch).
 *  It is the single place the surface names are spelled, and it is the ONLY face
 *  a script may reach. The op tokens in contract/op_registry.hpp are the plan
 *  side's vocabulary, consumed by pipeline::plan_gate: a script never spells them,
 *  and the window.* names above all are excluded by the frozen criterion below.
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
 *  contract::kLayers); recordable; executable; window_allowed.
 *
 *  holds_resource SEMANTICS (design-critic A3): TRUE means the call leaves the
 *  resource ALIVE after it returns; the caller must then release it explicitly
 *  (or hand it on). It does NOT mean "the call touches a resource". Values:
 *  backend = true (entering a backend keeps it resident), handoff = true (the
 *  handed-on child outlives the call), close = false (closing ends the
 *  resource's life with the call), writeop/copyop/settle/cfg = false.
 *
 *  WIDTH IS ALWAYS REQUIRED (design-critic A1, blocking): writeop and copyop
 *  must state the width. The profile's structure size is the VALIDATION
 *  criterion, not a default: a width that disagrees with it is refused with the
 *  named reason width-mismatch. No width is ever inferred.
 *
 *  HANDLES (design-critic A4): writeop, copyop, settle and handoff are
 *  HandleMethod rows and each carries a required handle argument, so the
 *  receiver binding is machine-checked rather than implied by the name.
 *  close is a handle method too. (Along the way the earlier rule "a handle
 *  method is never a window op" was dropped: after A4 three window ops ARE
 *  handle methods. window_allowed stays per-row data.)
 *
 *  WINDOW-LEVEL OPS ARE NOT SCRIPT-VISIBLE ENTRIES (user ruling): the names
 *  spelled window.* are a RUNTIME-INTERNAL PHASE GROUPING, not a script
 *  capability; the exposure table therefore contains no row whose name starts
 *  with "window.". Two machine criteria enforce it: the constexpr assertion
 *  surface_has_no_window_prefixed_names() below, and the grep performed by
 *  "make -C src check".
 *
 *  PHASE IS A STORED COLUMN (user ruling 2026-10-08): every row declares where
 *  it is used -- stage (cfg) / pre_window (backend, close) / window (writeop,
 *  copyop, settle) / terminal (handoff). It is NOT derived from window_allowed,
 *  because that derivation lost the granularity the design states. The
 *  vocabulary and the per-row assignment are pinned by assertions, and the
 *  manifest export carries the same column so documents can be checked against
 *  it.
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
 *  handoff carries terminal + payload_ref TOGETHER (design-critic A2):
 *  payload_ref names the payload declared in the document's payload section, so
 *  a handoff can never name a terminal without saying what it hands over.
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
#include <cstdint>
#include <string_view>

#include "contract/layer_scope.hpp"

namespace ghostlock::contract {
    /** Approved baseline: one script may not exceed 64 KB (A6 baseline item 2).
     *
     *  One fact, one definition: the sandbox enforces it as a script size budget
     *  and the plan loader enforces it as a delivery bound, and both layers already
     *  include this header, so the number lives here rather than twice. */
    inline constexpr std::size_t kScriptSizeCeiling = 64U * 1024U;
    /** How a surface name is used. */
    enum class SurfaceForm : unsigned char {
        Call = 0,
        Value = 1,
        HandleMethod = 2,
    };

    /** One keyed argument; max = schema acceptance ceiling, required = must the
     *  script pass it. */
    /** What kind of value an argument carries. Spelled once, here: the forwarding layer
     *  (script/call_sink.hpp) used to carry its own copy with different spellings, which is
     *  one concept split in two. The lower layer owns the vocabulary; the higher one reuses
     *  it, because that is the direction the include firewall allows. */
    enum class ArgKind : unsigned char {
        U64 = 0,     /**< A bounded unsigned number: max is the bound. */
        Str = 1,     /**< A bounded string: max is the bound in bytes. */
        Bool = 2,    /**< A boolean. */
        Handle = 3,  /**< A typed handle: passed back, never interpreted. */
    };

    struct ArgSpec final {
        const char *key;
        std::uint64_t max;
        bool required;
        /* Defaulted on purpose: the rows written before text arguments existed stay exactly as
         * they are, because a defaulted member does not change what they mean. A row that does
         * carry text says so, which is also what a reader greps for. */
        ArgKind kind = ArgKind::U64;
    };

    /* ---- execution phases: a STORED column (user ruling 2026-10-08) ----
     * The phase says WHERE in the execution plan a row is used, and it is
     * stored rather than derived, because the derived window/outside-window
     * pair threw away the granularity the design states:
     *   stage      - read-only snapshot taken during the stage phase (cfg)
     *   pre_window - construction / release around the window (backend, close)
     *   window     - executed atomically inside the PI window (writeop, copyop, settle)
     *   terminal   - triggers / hands over to the terminal phase (handoff) */
    enum class SurfacePhase : unsigned char {
        Stage = 0,
        PreWindow = 1,
        Window = 2,
        Terminal = 3,
    };

    inline constexpr const char *kSurfacePhaseNames[] = {
        "stage",
        "pre_window",
        "window",
        "terminal",
    };
    inline constexpr std::size_t kSurfacePhaseCount =
        sizeof(kSurfacePhaseNames) / sizeof(kSurfacePhaseNames[0]);

    [[nodiscard]] constexpr const char *surface_phase_name(SurfacePhase phase) noexcept {
        switch (phase) {
            case SurfacePhase::Stage: return kSurfacePhaseNames[0];
            case SurfacePhase::PreWindow: return kSurfacePhaseNames[1];
            case SurfacePhase::Window: return kSurfacePhaseNames[2];
            case SurfacePhase::Terminal: return kSurfacePhaseNames[3];
        }
        return "unknown";
    }

    /** One row of the exposure surface. */
    struct SurfaceEntry final {
        const char *name;
        SurfaceForm form;
        const ArgSpec *args;
        std::size_t arg_count;
        const char *implements_layer;
        SurfacePhase phase;
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

    /* ---- width validation: the profile's size is the criterion (A1) ---- */
    inline constexpr const char *kWidthMismatchReason = "width-mismatch";

    enum class WidthCheck : unsigned char {
        Match = 0,
        Mismatch = 1,
    };

    [[nodiscard]] constexpr const char *width_check_name(WidthCheck check) noexcept {
        switch (check) {
            case WidthCheck::Match: return "match";
            case WidthCheck::Mismatch: return kWidthMismatchReason;
        }
        return "unknown";
    }

    /** Compare the width a call states with the size the profile declares. */
    [[nodiscard]] constexpr WidthCheck width_check(std::uint64_t declared_width,
                                                   std::uint64_t profile_size) noexcept {
        return declared_width == profile_size ? WidthCheck::Match : WidthCheck::Mismatch;
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
        {"handle", kValueCeiling, true},       /* receiver binding (A4) */
        {"target_kind", 64U, true},            /* TargetKind name, never an offset */
        {"target_value", kValueCeiling, true}, /* profile-bounded */
        {"value", kValueCeiling, true},
        {"width", 8U, true},                   /* A1: never inferred */
    };
    /* 读法②（用户目标原文 ✓）：lua 直调底层 kernelaccess 的那一行。参数与 writeop 同形，另加 `op`
     *  选择器；本批只认 `write`（其余具名拒绝 ✓）⇒ **不新增内核行为** ✓。 */
    inline constexpr ArgSpec kKernelCallArgs[] = {
        {"handle", kValueCeiling, true},
        {"op", 64U, true},
        {"target_kind", 64U, true},
        {"target_value", kValueCeiling, true},
        {"value", kValueCeiling, true},
        {"width", 8U, true},
    };
    inline constexpr ArgSpec kCopyOpArgs[] = {
        {"handle", kValueCeiling, true},
        {"src_kind", 64U, true},
        {"src_value", kValueCeiling, true},
        {"dst_kind", 64U, true},
        {"dst_value", kValueCeiling, true},
        {"width", 8U, true},                   /* A1: never inferred */
    };
    inline constexpr ArgSpec kSettleArgs[] = {
        {"handle", kValueCeiling, true},
        {"us", 1000000U, true},
    };
    inline constexpr ArgSpec kHandoffArgs[] = {
        {"handle", kValueCeiling, true},
        {"terminal", 256U, true},
        {"payload_ref", 256U, true},           /* A2: what is handed over */
    };
    /* Encapsulated steps: the backend's own多相步骤, asked for by the name the wire declares
     *  (backend.<id>.steps). The row carries a handle like every other handle method, and the
     *  name is a bounded string: the step bodies live in the backend, never in this table. */
    inline constexpr ArgSpec kStepArgs[] = {
        {"handle", kValueCeiling, true},
        {"name", 64U, true},
    };
    /** The log verb's arguments: a level and a key, both text.
     *  Text rather than numbers because both are names; the bounds are the length bounds an
     *  over-long value is refused against, never truncated to (user ruling 2026-10-12: the
     *  verb is allowed; design-critic M2: the kind and the bound must be declarable). */
    inline constexpr ArgSpec kLogArgs[] = {
        {"level", 16U, true, ArgKind::Str},    /* debug / info only -- warn and error stay native */
        {"key", 256U, true, ArgKind::Str},     /* what the script is about to do */
    };

    /** Deprecated spelling kept out on purpose: steps go through their own row. */
    inline constexpr ArgSpec kCloseArgs[] = {
        {"handle", kValueCeiling, true},       /* slot + generation word */
    };

    /** The surface table: backend / writeop / copyop / settle / handoff / cfg /
     *  close. profile.get was removed (cfg replaces it), run was removed, the
     *  four operation rows are handle methods (A4), and no window.* row may
     *  appear (see the file comment). */
    inline constexpr SurfaceEntry kSurface[] = {
        {"backend", SurfaceForm::Call, nullptr, 0, "backend", SurfacePhase::PreWindow, true, false, false,
         true},
        {"writeop", SurfaceForm::HandleMethod, kWriteOpArgs, sizeof(kWriteOpArgs) / sizeof(kWriteOpArgs[0]),
         "backend", SurfacePhase::Window, true, true, true, false},
        {"kernelcall", SurfaceForm::HandleMethod, kKernelCallArgs,
         sizeof(kKernelCallArgs) / sizeof(kKernelCallArgs[0]), "backend", SurfacePhase::Window, true,
         true, true, false},
        {"copyop", SurfaceForm::HandleMethod, kCopyOpArgs, sizeof(kCopyOpArgs) / sizeof(kCopyOpArgs[0]), "backend",
         SurfacePhase::Window, true, true, true, false},
        {"settle", SurfaceForm::HandleMethod, kSettleArgs, sizeof(kSettleArgs) / sizeof(kSettleArgs[0]), "backend",
         SurfacePhase::Window, true, true, true, false},
        {"handoff", SurfaceForm::HandleMethod, kHandoffArgs, sizeof(kHandoffArgs) / sizeof(kHandoffArgs[0]),
         "terminal", SurfacePhase::Terminal, true, true, false, true},
        {"step", SurfaceForm::HandleMethod, kStepArgs, sizeof(kStepArgs) / sizeof(kStepArgs[0]), "backend",
         SurfacePhase::Window, true, true, true, false},
        {"cfg", SurfaceForm::Value, nullptr, 0, "profile", SurfacePhase::Stage, true, false, false, false},
        {"close", SurfaceForm::HandleMethod, kCloseArgs, sizeof(kCloseArgs) / sizeof(kCloseArgs[0]), "session",
         SurfacePhase::PreWindow, true, true, false, false},
        /* Eighth name (user ruling 2026-10-12: allowed in full; the count text in
         * op_registry.hpp changes with it, and the 沿革 is recorded in
         * docs/analysis/43499-plan-lua-log-verb.md 8). Stage, not the window: a log line
         * never belongs inside the window body (pi_window.hpp:15), and window_allowed is
         * false to say so rather than leaving it to a reader's memory. */
        {"log", SurfaceForm::Call, kLogArgs, sizeof(kLogArgs) / sizeof(kLogArgs[0]), "backend",
         SurfacePhase::Stage, true, true, false, false},
    };
    inline constexpr std::size_t kSurfaceCount = sizeof(kSurface) / sizeof(kSurface[0]);

    /** Argument counts and key names frozen for the report and the tests. Six since 2026-10-10:
     *  the encapsulated step row joined the five that were there. */
    /* 读法②（用户目标原文 ✓）加了一行 `kernelcall` ⇒ 由 6 变 7（pin 已重生成 ✓）。 */
    inline constexpr std::size_t kHandleMethodRowCount = 7;

    /** Lookup outcome: found, or the named rejection surface-unknown-name. */
    enum class SurfaceLookup : unsigned char {
        Found = 0,
        UnknownSymbol = 1,
    };

    [[nodiscard]] constexpr const char *surface_lookup_name(SurfaceLookup code) noexcept {
        switch (code) {
            case SurfaceLookup::Found: return "found";
            case SurfaceLookup::UnknownSymbol: return "surface-unknown-name";
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

    /** True when the named argument is declared AND required. */
    [[nodiscard]] constexpr bool surface_row_requires_arg(const SurfaceEntry &row, const char *key) noexcept {
        for (std::size_t a = 0; a < row.arg_count; ++a) {
            if (surface_same_text(row.args[a].key, key)) {
                return row.args[a].required;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr const SurfaceEntry *surface_row(const char *name) noexcept {
        const SurfaceResult result = lookup(name);
        return result.entry;
    }

    /** The write target is the PAIR {target_kind, target_value}: both or neither. */
    [[nodiscard]] constexpr bool writeop_target_is_a_pair() noexcept {
        const SurfaceEntry *row = surface_row("writeop");
        if (row == nullptr) {
            return false;
        }
        const bool kind = surface_row_has_arg(*row, "target_kind");
        const bool value = surface_row_has_arg(*row, "target_value");
        return kind && value;
    }

    /** copyop carries the same tagged pair on BOTH sides, plus a width. */
    [[nodiscard]] constexpr bool copyop_sides_are_symmetric() noexcept {
        const SurfaceEntry *row = surface_row("copyop");
        if (row == nullptr) {
            return false;
        }
        return surface_row_has_arg(*row, "src_kind") && surface_row_has_arg(*row, "src_value") &&
               surface_row_has_arg(*row, "dst_kind") && surface_row_has_arg(*row, "dst_value") &&
               surface_row_has_arg(*row, "width");
    }

    /** A1: the width is required on both writing operations -- never inferred. */
    [[nodiscard]] constexpr bool widths_are_required() noexcept {
        const SurfaceEntry *write = surface_row("writeop");
        const SurfaceEntry *copy = surface_row("copyop");
        if (write == nullptr || copy == nullptr) {
            return false;
        }
        return surface_row_requires_arg(*write, "width") && surface_row_requires_arg(*copy, "width");
    }

    /** A2: a handoff names both the terminal and the payload it hands over. */
    [[nodiscard]] constexpr bool handoff_carries_terminal_and_payload_ref() noexcept {
        const SurfaceEntry *row = surface_row("handoff");
        if (row == nullptr) {
            return false;
        }
        const bool terminal = surface_row_requires_arg(*row, "terminal");
        const bool payload = surface_row_requires_arg(*row, "payload_ref");
        return terminal && payload;
    }

    /** A4: the four operations are handle methods, each binding its receiver. */
    [[nodiscard]] constexpr bool operations_are_handle_methods() noexcept {
        const char *const operations[] = {"writeop", "copyop", "settle", "handoff"};
        for (const char *name : operations) {
            const SurfaceEntry *row = surface_row(name);
            if (row == nullptr) {
                return false;
            }
            if (row->form != SurfaceForm::HandleMethod) {
                return false;
            }
            if (!surface_row_requires_arg(*row, "handle")) {
                return false;
            }
        }
        return true;
    }

    /** Every handle method declares the handle it acts on. */
    [[nodiscard]] constexpr bool handle_methods_are_well_formed() noexcept {
        for (std::size_t i = 0; i < kSurfaceCount; ++i) {
            if (kSurface[i].form != SurfaceForm::HandleMethod) {
                continue;
            }
            if (!surface_row_requires_arg(kSurface[i], "handle")) {
                return false;
            }
        }
        return true;
    }

    /** A3: the values that carry the resource-past-return semantics. */
    [[nodiscard]] constexpr bool resource_semantics_are_pinned() noexcept {
        const SurfaceEntry *backend = surface_row("backend");
        const SurfaceEntry *close = surface_row("close");
        const SurfaceEntry *handoff = surface_row("handoff");
        if (backend == nullptr || close == nullptr || handoff == nullptr) {
            return false;
        }
        return backend->holds_resource && !close->holds_resource && handoff->holds_resource;
    }

    /** A5: the unknown-name refusal is spelled surface-unknown-name. */
    [[nodiscard]] constexpr bool lookup_refusal_is_pinned() noexcept {
        return surface_same_text(surface_lookup_name(SurfaceLookup::UnknownSymbol), "surface-unknown-name");
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

    /** The declared phase vocabulary: four, unique, non-empty. */
    [[nodiscard]] constexpr bool phase_names_are_closed() noexcept {
        if (kSurfacePhaseCount != 4) {
            return false;
        }
        for (std::size_t i = 0; i < kSurfacePhaseCount; ++i) {
            if (kSurfacePhaseNames[i] == nullptr || kSurfacePhaseNames[i][0] == '\0') {
                return false;
            }
            for (std::size_t j = i + 1; j < kSurfacePhaseCount; ++j) {
                if (surface_same_text(kSurfacePhaseNames[i], kSurfacePhaseNames[j])) {
                    return false;
                }
            }
        }
        return true;
    }

    /** The per-row phase assignment the design states (stored, not derived). */
    [[nodiscard]] constexpr bool phases_are_assigned_per_design() noexcept {
        struct Expectation final {
            const char *name;
            SurfacePhase phase;
        };
        const Expectation expected[] = {
            {"cfg", SurfacePhase::Stage},
            {"backend", SurfacePhase::PreWindow},
            {"close", SurfacePhase::PreWindow},
            {"writeop", SurfacePhase::Window},
            {"copyop", SurfacePhase::Window},
            {"settle", SurfacePhase::Window},
            {"handoff", SurfacePhase::Terminal},
        };
        for (const Expectation &want : expected) {
            const SurfaceEntry *row = surface_row(want.name);
            if (row == nullptr || row->phase != want.phase) {
                return false;
            }
        }
        return true;
    }

    static_assert(kSurfaceCount >= 1, "the exposure surface must not be empty");
    static_assert(phase_names_are_closed(), "the phase vocabulary is closed at four names");
    static_assert(phases_are_assigned_per_design(),
                  "stage=cfg, pre_window=backend/close, window=writeop/copyop/settle, terminal=handoff");
    static_assert(surface_names_are_unique_and_non_empty(), "surface names must be unique and non-empty");
    static_assert(surface_has_no_window_prefixed_names(),
                  "window.* is a runtime-internal phase grouping, never a script surface entry");
    static_assert(surface_has_no_run_row(), "the shell entry was removed: no run row may reappear");
    static_assert(surface_layers_exist(), "every surface row must name a declared layer");
    static_assert(surface_args_are_well_formed(), "argument specs need unique keys and non-zero ceilings");
    static_assert(writeop_target_is_a_pair(), "writeop targets are the tagged pair {target_kind, target_value}");
    static_assert(copyop_sides_are_symmetric(), "copyop needs the tagged pair on both sides plus a width");
    static_assert(widths_are_required(), "A1: writeop and copyop must require width (never inferred)");
    static_assert(handoff_carries_terminal_and_payload_ref(),
                  "A2: handoff names the terminal AND the payload it hands over");
    static_assert(operations_are_handle_methods(), "A4: writeop/copyop/settle/handoff are handle methods");
    static_assert(handle_methods_are_well_formed(), "every handle method binds the handle it acts on");
    static_assert(resource_semantics_are_pinned(), "A3: backend/handoff hold a resource, close does not");
    static_assert(lookup_refusal_is_pinned(), "A5: the refusal is spelled surface-unknown-name");
    static_assert(target_kind_names_are_closed(), "the target-kind set is closed at three names");
    static_assert(surface_flags_are_consistent(),
                  "flags must stay consistent (window/holds_resource/Value-is-read-only)");
}  // namespace ghostlock::contract
