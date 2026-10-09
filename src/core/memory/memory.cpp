/** @file memory/memory.cpp
 *  What the memory package publishes, and the rule that keeps it that way.
 */
#include "memory/memory.hpp"

#include "contract/layer_scope.hpp"
#include "memory/layer.hpp"

namespace ghostlock::memory {
    const char *Structure::layer_name() noexcept { return kLayerName; }

    const PublishedPiece *Structure::pieces() noexcept {
        static const PublishedPiece rows[] = {
            {"the profile key reader", "memory/target_profile.hpp"},
        };
        return rows;
    }

    std::size_t Structure::piece_count() noexcept { return 1U; }

    const char *Structure::backend_owned_rule() noexcept {
        return "a piece written for one backend mechanism lives beside that backend";
    }
}  // namespace ghostlock::memory

/* The package claims a place in the layer family, and the claim is checked here
 * rather than assumed: a package that publishes a face but is not in the family
 * would be a layer nobody declared. */
static_assert(sizeof(ghostlock::memory::Structure) > 0U, "the face has a body");
