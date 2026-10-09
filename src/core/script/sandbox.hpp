#pragma once
/** @file script/sandbox.hpp
 *  The Lua sandbox (A6-1): whitelist assembly, four budgets, surface
 *  registration driven by contract::kSurface, a read-only cfg snapshot, and a
 *  stage-only entry point. It forwards calls through script::CallSink and does
 *  not model plans (A6-2 owns PlanOp[]).
 *
 *  Assembly rule (A6 baseline item 2 / ADR-0008 3.1): luaopen_base and
 *  luaL_openlibs are NEVER called. The state is created with lua_newstate over a
 *  bounded allocator and the kept globals are registered one by one, so
 *  load/loadfile/dofile/print/io/os/package/debug/coroutine/require do not exist
 *  at runtime at all.
 *
 *  Budgets (all four enforced, each breach is the named refusal budget-exceeded):
 *  instruction count (a counting hook), memory hard limit (the bounded
 *  allocator), stack depth (measured in the same hook), and the single-script
 *  size ceiling. ALL FOUR ARE MECHANISM CONSTANTS: ADR-0008 says the sandbox
 *  parameters are execution-time hard constraints that a profile may not widen,
 *  so they are not profile keys and this header must not read as if they were.
 *  The size ceiling has its approved value (A6 item 2: one script <= 64 KB); the
 *  other three AWAIT A BASELINE LINE and carry no value here until it lands. A
 *  bound of zero therefore means NOT CONFIGURED, which is a refusal of its own:
 *  open() fails and the caller reports the missing tuning, so an unconfigured
 *  run is never mistaken for a script that overran.
 *
 *  Phase: the sandbox is STAGE-only. The entry point takes NO phase parameter at
 *  all, so a window phase cannot even be expressed by a caller (structural
 *  guarantee); kSandboxPhase records the phase for the report and the tests.
 *  The script layer must not include backend/pipeline/terminal (include firewall).
 *
 *  cfg: set_cfg_snapshot installs a read-only table. The field set and values
 *  belong to the injector (A6-2 / the loader); a script write attempt is refused.
 */
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "contract/script_surface.hpp"
#include "script/call_sink.hpp"

namespace ghostlock::script {
    /* The script size ceiling moved to contract/script_surface.hpp (mechanical
     * move, no behaviour change): it is one fact shared by the sandbox and by the
     * plan loader, and contract/ is the layer both of them already include, so the
     * number is defined once rather than once per layer. */

    /** The sandbox belongs to the stage phase (no window phase is expressible). */
    inline constexpr contract::SurfacePhase kSandboxPhase = contract::SurfacePhase::Stage;
    static_assert(kSandboxPhase == contract::SurfacePhase::Stage,
                  "the sandbox runs outside the window: stage only");

    /** The four mechanism bounds. Carried as a struct so a test can drive the
     *  boundary cases, not because anything may set them at run time: the values are
     *  the mechanism's own (see the note above), three of them still awaiting the
     *  baseline line that authorises a number. Zero means not configured. */
    struct ScriptBudget final {
        std::uint64_t max_instructions = 0;
        std::uint64_t max_bytes = 0;
        std::uint32_t max_stack_depth = 0;
        std::uint32_t max_script_bytes = 0;
    };

    /** True when every bound has been given a value. A budget that is not configured
     *  is refused where it is configured rather than where it is used: a zero bound
     *  turned into budget-exceeded would report an unconfigured run as a script that
     *  overran, and those are different failures with different fixes. */
    [[nodiscard]] constexpr bool script_budget_is_configured(const ScriptBudget &budget) noexcept {
        return budget.max_instructions != 0U && budget.max_bytes != 0U &&
               budget.max_stack_depth != 0U && budget.max_script_bytes != 0U;
    }

    /** Named refusals the sandbox itself produces (UML 3.2 registry). */
    enum class ScriptRejection : unsigned char {
        None = 0,
        BudgetExceeded = 1,
        SurfaceUnknownName = 2,
        PlanSyntax = 3,
        CfgReadOnly = 4,
        CfgUnknownField = 5,
        HandleStale = 6,
        HandleConflict = 7,
        /** The mechanism's bounds have no values yet: the run was never configured,
         *  which is a different failure from a script that ran past a bound. The
         *  name is the tree's existing one for a tuning value that is not there. */
        TuningMissing = 8,
    };

    [[nodiscard]] const char *script_rejection_name(ScriptRejection rejection) noexcept;

    /** Globals the HOST injects (not part of the Lua whitelist classes): the
     *  surface table and the read-only cfg snapshot. Declared once so the
     *  globals-set assertion names them instead of hand-listing them. */
    inline constexpr const char *kHostGlobalNames[] = {
        "surface",
        "cfg",
    };
    inline constexpr std::size_t kHostGlobalCount = sizeof(kHostGlobalNames) / sizeof(kHostGlobalNames[0]);

    /** One field of the read-only cfg snapshot. */
    struct CfgField final {
        const char *name;
        std::uint64_t value;
    };

    /** The sandbox. One state, opened once, run any number of times. */
    class Sandbox final {
      public:
        Sandbox() noexcept = default;
        ~Sandbox() noexcept;
        Sandbox(const Sandbox &) = delete;
        Sandbox &operator=(const Sandbox &) = delete;

        /** Open with the injected budgets and forward target. The absence of a
         *  phase parameter IS the structural guarantee (stage only). */
        [[nodiscard]] bool open(const ScriptBudget &budget, CallSink sink) noexcept;

        /** Install the read-only cfg snapshot (field set and values are the
         *  injector's; the sandbox only guarantees it cannot be written). */
        [[nodiscard]] bool set_cfg_snapshot(const CfgField *fields, std::size_t count) noexcept;

        /** Compile and run one script inside the budgets. */
        [[nodiscard]] ScriptRejection run(std::string_view script) noexcept;

        void close() noexcept;

        /* ---- evidence hooks (assertions and the report) ---- */
        [[nodiscard]] bool is_open() const noexcept;
        [[nodiscard]] std::size_t registered_global_count() const noexcept;
        [[nodiscard]] std::size_t registered_surface_count() const noexcept;
        [[nodiscard]] std::uint64_t bytes_used() const noexcept;
        [[nodiscard]] std::uint64_t instructions_left() const noexcept;
        [[nodiscard]] std::size_t last_call_count() const noexcept;
        [[nodiscard]] std::size_t last_error_count() const noexcept;
        [[nodiscard]] ScriptRejection last_rejection() const noexcept;

        /** Set equality between a Lua table's string keys and a name list: true
         *  only when every listed name is present AND no other key exists.
         *  Pass table_name == nullptr to inspect the GLOBALS table itself. */
        [[nodiscard]] bool table_keys_match(const char *table_name, const char *const *names,
                                            std::size_t count) noexcept;

        /** True when the named global is absent (nil) inside the sandbox. */
        [[nodiscard]] bool global_absent(const char *name) noexcept;

        /** Implementation detail, exposed so the file-local allocator and hook
         *  helpers can name it; callers must not use it. */
        struct State;

      private:
        State *state_ = nullptr;
    };
}  // namespace ghostlock::script
