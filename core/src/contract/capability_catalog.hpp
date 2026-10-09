#pragma once
/** @file contract/capability_catalog.hpp
 *  What an interface declares it needs: the capability catalogue, in one place.
 *
 *  ADR-0012 puts two facts in two files: what the interface declares lives here, in the top
 *  package, and what each backend provides lives under that backend. This is the first of the
 *  two; the agreement between them is what a test holds, and the catalogue on its own is only
 *  the promise.
 *
 *  Two levels, and the design says why the second exists. An effect capability is a result a
 *  step has to bring about, and the names are the step effect vocabulary that already exists,
 *  so the three steps the user named are covered without a second set of words. A mechanism
 *  capability is a piece of machinery a step needs to run at all, and those names are new: they
 *  are capability names rather than file paths, and each one is bound to its implementation in
 *  the backend's provision table, so a rename there fails to build rather than drifting.
 *
 *  Nothing here says whether a backend has it. That is the other file.
 */
#include <cstddef>
#include <string_view>

#include "contract/step_catalog.hpp"

namespace ghostlock::contract {
    /** Which level a capability is: a result, or a piece of machinery. */
    enum class CapabilityLevel : unsigned char {
        /** A result a step brings about. */
        Effect = 0,
        /** A mechanism a step needs in order to run. */
        Mechanism = 1,
    };

    /** One capability the interface declares. */
    struct CapabilitySpec final {
        /** The name, and the only place it is spelled. */
        std::string_view name;
        CapabilityLevel level;
        /** One sentence saying what it means. */
        std::string_view meaning;
        /** Whether every backend that carries this interface must provide it. A required
         *  capability that is not provided is a broken promise, and the agreement test says so
         *  at build time; an optional one missing is allowed, and is refused by name when
         *  something selects it. */
        bool required;
    };

    /** The catalogue. Effect names come from the step catalogue; mechanism names are declared
     *  here for the first time and bound to implementations in the backends. */
    inline constexpr CapabilitySpec kCapabilityCatalog[] = {
        {"Selinux", CapabilityLevel::Effect, "the step leaves selinux not enforcing", true},
        {"Credentials", CapabilityLevel::Effect, "the step forges credentials", true},
        {"Seccomp", CapabilityLevel::Effect, "the step removes the seccomp filter", true},
        {"PageCache", CapabilityLevel::Effect, "the step writes through the page cache", false},
        {"futex_collision_search", CapabilityLevel::Mechanism,
         "find an object by colliding futexes across a range", true},
        {"kernel_page_prepare", CapabilityLevel::Mechanism,
         "prepare the kernel page a write will land on", true},
        {"arbitrary_write", CapabilityLevel::Mechanism, "land one write at a kernel address", true},
        {"perf_task_sampling", CapabilityLevel::Mechanism,
         "find a task by sampling its registers", false},
    };
    inline constexpr std::size_t kCapabilityCount = sizeof(kCapabilityCatalog) / sizeof(kCapabilityCatalog[0]);

    /** The catalogue has to hold together on its own before anything compares it with a
     *  backend: names are unique, none is empty, and each names its meaning. A duplicate name
     *  would make every later comparison ambiguous, which is the failure this whole file exists
     *  to prevent. */
    [[nodiscard]] constexpr bool capabilities_are_well_formed() noexcept {
        for (std::size_t i = 0U; i < kCapabilityCount; ++i) {
            const auto &row = kCapabilityCatalog[i];
            if (row.name.empty() || row.meaning.empty()) {
                return false;
            }
            for (std::size_t j = i + 1U; j < kCapabilityCount; ++j) {
                const auto &other = kCapabilityCatalog[j];
                if (row.name.size() == other.name.size()) {
                    bool same = true;
                    for (std::size_t c = 0U; c < row.name.size(); ++c) {
                        if (row.name[c] != other.name[c]) {
                            same = false;
                            break;
                        }
                    }
                    if (same) {
                        return false;
                    }
                }
            }
        }
        return true;
    }
    static_assert(capabilities_are_well_formed(), "the capability catalogue is not well formed");

    /** The row for a name, or null. */
    [[nodiscard]] constexpr const CapabilitySpec *capability_named(std::string_view wanted) noexcept {
        for (std::size_t i = 0U; i < kCapabilityCount; ++i) {
            const auto &row = kCapabilityCatalog[i];
            if (row.name.size() != wanted.size()) {
                continue;
            }
            bool same = true;
            for (std::size_t c = 0U; c < row.name.size(); ++c) {
                if (row.name[c] != wanted[c]) {
                    same = false;
                    break;
                }
            }
            if (same) {
                return &row;
            }
        }
        return nullptr;
    }
}  // namespace ghostlock::contract
