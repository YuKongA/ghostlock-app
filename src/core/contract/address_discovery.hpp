#pragma once
/** @file contract/address_discovery.hpp
 *  Finding the kernel addresses an attack needs, as a contract rather than a mechanism.
 *
 *  Two ways to find them exist in this tree -- the collision search and a perf sample
 *  poll -- and a caller should not have to know which it was handed. What both must
 *  agree on is the part that matters when they fail: a discovery that did not succeed
 *  reports so, and reports nothing else. No partial address, no half-filled field, no
 *  plausible guess: an attack built on an address that was not found is an attack
 *  built on whatever happened to be in that memory, and the failure would surface
 *  far from its cause.
 *
 *  The shape is a concept so a mechanism can be checked against it where it is
 *  written, and the result is a plain struct so a test can compare one against
 *  another value by value.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace ghostlock::contract {
    /** What a discovery found. All zero means it found nothing; that is the only
     *  representation of failure, so a caller cannot mistake a partial answer for a
     *  complete one. */
    struct AddressDiscoveryResult final {
        bool ok = false;
        std::uint64_t kaslr_base = 0U;
        std::uint64_t init_task = 0U;
        std::uint64_t target_task = 0U;
        std::uint64_t mm_struct = 0U;
    };

    /** The result of having found nothing: every field zero, ok false. */
    [[nodiscard]] constexpr AddressDiscoveryResult discovery_failed() noexcept {
        return AddressDiscoveryResult{};
    }

    /** True when a result carries a complete answer: ok, and every address listed
     *  above non-zero. A mechanism that sets ok without filling a field is not
     *  reporting success, whatever it thinks. */
    [[nodiscard]] constexpr bool discovery_is_complete(
        const AddressDiscoveryResult &result) noexcept {
        return result.ok && result.kaslr_base != 0U && result.init_task != 0U &&
               result.target_task != 0U && result.mm_struct != 0U;
    }

    /** What a mechanism has to answer to be usable as a discovery: fill an out
     *  parameter and say whether it succeeded. On a false return the out parameter
     *  must be discovery_failed(), which is the rule a caller relies on. */
    template <class Discovery>
    concept AddressDiscoveryOps = requires(AddressDiscoveryResult out) {
        { Discovery::discover(out) } -> std::same_as<bool>;
    };
}  // namespace ghostlock::contract
