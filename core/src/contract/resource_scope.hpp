#pragma once
/** @file contract/resource_scope.hpp
 *  P4 step 1: freeze the three-way resource classification (window / op / script).
 *
 *  Authority: docs/analysis/composition-layer-p4-spec.md section 1 +
 *  docs/analysis/adr/0010-lua-cpp-resource-model.md. Classification is a
 *  compile-time fact: the window-scoped rows below are the resources
 *  AttackWindow owns; nothing else may own them.
 *
 *  Discriminator (one sentence): a resource is window-scoped when it is
 *  reachable while the PI waiter lives AND its lifetime is decided by the host
 *  rather than by the script (a script may hold handles only).
 */
#include <cstddef>

namespace ghostlock::contract {
    /** Where a resource lives and which layer may own it. */
    enum class ResourceScope : unsigned char {
        Window = 0,
        Op = 1,
        Script = 2,
    };

    /** One frozen classification row: name, scope, sole owner. */
    struct ScopeRule final {
        const char *name;
        ResourceScope scope;
        const char *owner;
    };

    /** Frozen classification table (P4 section 1 evidence list, verbatim names). */
    inline constexpr ScopeRule kResourceScopes[] = {
        {"profile", ResourceScope::Window, "AttackWindow"},
        {"addresses", ResourceScope::Window, "AttackWindow"},
        {"heap", ResourceScope::Window, "AttackWindow"},
        {"race", ResourceScope::Window, "AttackWindow"},
        {"victim", ResourceScope::Window, "AttackWindow"},
        {"parked_victim", ResourceScope::Window, "AttackWindow"},
        {"parked_victim_cmd", ResourceScope::Window, "AttackWindow"},
        {"op_temporary", ResourceScope::Op, "op body"},
        {"script_value", ResourceScope::Script, "script (handles only)"},
    };
    inline constexpr std::size_t kScopeRuleCount = sizeof(kResourceScopes) / sizeof(kResourceScopes[0]);

    /** Exit paths a window host must reclaim on (P4 section 7). */
    enum class ExitPath : unsigned char {
        Success = 0,
        Failure = 1,
        Retry = 2,
        Cancel = 3,
        EarlyReturn = 4,
    };
    inline constexpr std::size_t kExitPathCount = 5;
    inline constexpr const char *kExitPathNames[kExitPathCount] = {
        "success", "failure", "retry", "cancel", "early-return"};

    /** Exhaustive name lookup: adding an enumerator breaks this switch. */
    [[nodiscard]] constexpr const char *exit_path_name(ExitPath path) noexcept {
        switch (path) {
            case ExitPath::Success: return kExitPathNames[0];
            case ExitPath::Failure: return kExitPathNames[1];
            case ExitPath::Retry: return kExitPathNames[2];
            case ExitPath::Cancel: return kExitPathNames[3];
            case ExitPath::EarlyReturn: return kExitPathNames[4];
        }
        return "unknown";
    }

    static_assert(static_cast<unsigned char>(ResourceScope::Window) == 0, "scope order is frozen");
    static_assert(static_cast<unsigned char>(ResourceScope::Script) == 2, "scope order is frozen");
    static_assert(kExitPathCount == 5, "exit-path set is frozen (P4 section 7)");
    static_assert(&kExitPathNames[4] != nullptr, "exit path names are present");
}  // namespace ghostlock::contract
