#pragma once
/** @file script/lua_whitelist.hpp
 *  The Lua global vocabulary in three classes -- the single authority for those
 *  names in this tree.
 *
 *  Source: A6 baseline item 2 (approved 2026-10-08) and ADR-0008 background
 *  section 3.1. The base library is NEVER opened (luaopen_base / luaL_openlibs
 *  are not called), so every kept global is registered by the sandbox itself.
 *
 *   - kept (15): assert error type tostring tonumber pairs ipairs next select
 *     pcall xpcall rawget rawset rawequal rawlen
 *   - budgeted (1): collectgarbage -- present, but the memory budget still
 *     applies to whatever it does
 *   - excluded (7 names / 6 classes): load loadfile dofile print warn
 *     setmetatable getmetatable (metatables are not exposed at all)
 */
#include <cstddef>

namespace ghostlock::script {
    inline constexpr const char *kKeptGlobalNames[] = {
        "assert",   "error",  "type",    "tostring", "tonumber", "pairs",   "ipairs", "next",
        "select",   "pcall",  "xpcall",  "rawget",   "rawset",   "rawequal", "rawlen",
    };
    inline constexpr std::size_t kKeptGlobalCount = sizeof(kKeptGlobalNames) / sizeof(kKeptGlobalNames[0]);

    inline constexpr const char *kBudgetedGlobalNames[] = {
        "collectgarbage",
    };
    inline constexpr std::size_t kBudgetedGlobalCount =
        sizeof(kBudgetedGlobalNames) / sizeof(kBudgetedGlobalNames[0]);

    inline constexpr const char *kExcludedGlobalNames[] = {
        "load", "loadfile", "dofile", "print", "warn", "setmetatable", "getmetatable",
    };
    inline constexpr std::size_t kExcludedGlobalCount =
        sizeof(kExcludedGlobalNames) / sizeof(kExcludedGlobalNames[0]);

    /* Structural: the classes are exactly as approved (15 / 1 / 7). */
    static_assert(kKeptGlobalCount == 15, "the kept class is fifteen names (A6 baseline item 2)");
    static_assert(kBudgetedGlobalCount == 1, "one global is kept under the budget");
    static_assert(kExcludedGlobalCount == 7, "the excluded class is seven names / six classes");
}  // namespace ghostlock::script
