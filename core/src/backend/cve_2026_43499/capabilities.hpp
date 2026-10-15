#pragma once
/** @file backend/cve_2026_43499/capabilities.hpp
 *  What this backend provides, in one place, with the binding spelled as a type.
 *
 *  ADR-0012 puts the declaration in the top package and the provision here. This is the second
 *  of the two, and it answers one question only: given a capability name, does this backend have
 *  an implementation, and which one. Whether the device can run it is a different fact and lives
 *  elsewhere: the sampling path, for instance, is implemented here and does not deliver register
 *  sets on the device this was developed against, which is a runtime conclusion a static table
 *  cannot carry.
 *
 *  The binding is a static assertion about the entry point's type rather than a string. A path in
 *  a string can be renamed away and the table would still compile, which is the drift this
 *  arrangement exists to prevent; naming the function in a type means a rename fails the build.
 */
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "backend/cve_2026_43499/leak/snitch_setup.hpp"
#include "backend/cve_2026_43499/memory/reclaim_plan.hpp"
#include "backend/cve_2026_43499/perf_vote.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    /** One capability this backend provides. */
    struct ProvisionedCapability final {
        /** The name, spelled as the catalogue spells it. */
        std::string_view name;
        /** The implementation, as a path for a reader. The binding that cannot drift is the
         *  assertion below this table. */
        std::string_view implementation;
        /** Whether an implementation exists at all. A false row is a capability this backend
         *  does not have, and saying so is the point of the table. */
        bool implemented;
    };

    /** What this backend provides. */
    inline constexpr ProvisionedCapability kProvisioned[] = {
        {"Selinux", "backend/cve_2026_43499/steps (the step that disenforces)", false},
        {"Credentials", "backend/cve_2026_43499/memory/credential_image", true},
        {"Seccomp", "backend/cve_2026_43499/steps (the step that bypasses)", false},
        {"PageCache", "(not this backend)", false},
        {"futex_collision_search", "backend/cve_2026_43499/leak (walk, hash, count)", true},
        {"kernel_page_prepare", "backend/cve_2026_43499/memory (geometry, plans, ops)", true},
        {"arbitrary_write", "(the write primitive itself is not implemented)", false},
        {"perf_task_sampling", "backend/cve_2026_43499/perf_{ring,vote,landing}", true},
    };
    inline constexpr std::size_t kProvisionedCount = sizeof(kProvisioned) / sizeof(kProvisioned[0]);

    /** The bindings. Each names the entry point of the capability in a type, so a rename or a
     *  deletion stops the build instead of leaving a row that points at nothing. A capability
     *  with no implementation has no assertion to make, and its false row above says so. */
    static_assert(
        std::is_same_v<decltype(&leak::build_snitch_setup), leak::SnitchSetupVerdict (*)(const leak::SnitchInputs &, leak::SnitchSetup &) noexcept>,
        "futex_collision_search binds to the search setup entry point");
    static_assert(std::is_same_v<decltype(&memory::plan_reclaim),
                                 memory::ReclaimVerdict (*)(const memory::ReclaimCounts &, memory::ReclaimStep *,
                                                           std::size_t, std::size_t &) noexcept>,
                  "kernel_page_prepare binds to the reclaim plan entry point");
    static_assert(std::is_same_v<decltype(&vote_for_task), std::uint64_t (*)(const std::uint64_t *, std::size_t) noexcept>,
                  "perf_task_sampling binds to the voting entry point");
}  // namespace ghostlock::backend::cve_2026_43499
