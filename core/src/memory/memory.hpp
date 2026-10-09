#pragma once
/** @file memory/memory.hpp
 *  The memory package's own top-level structure: what this package publishes.
 *
 *  What belongs here is the test the package's name implies: a piece is memory when
 *  it has no backend in it. The neutral parts stay -- a key-value reader over the
 *  profile, which any backend may use -- and anything written for one backend's
 *  mechanism lives beside that backend, which is where the write primitives and the
 *  page reclaim now are. This header states that face so a reader does not have to
 *  infer it from a directory listing.
 */
#include <cstddef>

namespace ghostlock::memory {
    /** One thing this package publishes, and where it lives. */
    struct PublishedPiece final {
        const char *name;
        const char *header;
    };

    /** The package's face. */
    class Structure final {
      public:
        /** The layer this package is in the family as. */
        [[nodiscard]] static const char *layer_name() noexcept;
        /** What the package publishes: the neutral pieces, and only those. */
        [[nodiscard]] static const PublishedPiece *pieces() noexcept;
        [[nodiscard]] static std::size_t piece_count() noexcept;
        /** Where a backend's own memory mechanism lives, as a rule rather than a
         *  path: beside the backend that owns it. */
        [[nodiscard]] static const char *backend_owned_rule() noexcept;
    };
}  // namespace ghostlock::memory
