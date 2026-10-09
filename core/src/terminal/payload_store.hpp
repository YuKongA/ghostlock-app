#pragma once
/** @file terminal/payload_store.hpp
 *  A6-4: putting a validated payload where the terminal phase can run it.
 *
 *  The terminal owns the path; the document only names the payload. Writing is
 *  atomic in the sense that matters here: the bytes go to a sibling temporary,
 *  are flushed, and only then does a rename make them visible under the final
 *  name, so a reader never sees a half-written payload. The file is created
 *  with mode 0600 and never widened.
 *
 *  This is the only place in the terminal path that touches the filesystem, and
 *  nothing here runs before the terminal phase: stage and the window never call
 *  it, which is what keeps the earlier layers free of I/O.
 */
#include <cstddef>
#include <cstdint>

namespace ghostlock::terminal {
    /** What the caller gets back: where it went, and why not when it failed. */
    struct StoreResult final {
        bool ok = false;
        /** The final path, bounded; empty when nothing was written. */
        char path[192] = {};
        /** A registered refusal name (see UML 3.2), never an invented one. */
        const char *reason = "payload-missing";
    };

    /** Write size bytes of a validated payload into directory/name with mode
     *  0600, atomically. The caller has already checked the payload's digest. */
    [[nodiscard]] StoreResult store_payload(const char *directory, const char *name,
                                            const std::uint8_t *bytes,
                                            std::size_t size) noexcept;

    /** Remove a payload again (the terminal may clean up after a refusal). */
    [[nodiscard]] bool remove_payload(const char *path) noexcept;
}  // namespace ghostlock::terminal
