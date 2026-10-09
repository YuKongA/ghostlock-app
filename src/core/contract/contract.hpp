#pragma once
/** @file contract/contract.hpp
 *  The contract package's own top-level structure: what this package publishes.
 *
 *  Every package in this tree is read by two kinds of reader -- someone looking for
 *  the vocabulary, and someone checking the tree still matches it -- and both of them
 *  want one place to start. This is that place for contract: the layer family, the
 *  catalogues this package owns, and the size of each, so a reader can see the
 *  package's face in one screen and a test can assert it has not drifted.
 *
 *  Nothing here decides anything: every number is read from the table that owns it.
 */
#include <cstddef>

#include "contract/chain_catalog.hpp"
#include "contract/kmi_catalog.hpp"
#include "contract/layer_scope.hpp"
#include "contract/op_registry.hpp"

namespace ghostlock::contract {
    /** One table this package publishes, with the size it actually has. */
    struct PublishedTable final {
        const char *name;
        std::size_t rows;
    };

    /** The package's face: the layers, and the tables built on them. */
    class Structure final {
      public:
        /** The layers, in dependency order (an earlier layer may not include a
         *  later one). */
        [[nodiscard]] static const PublishedTable *layers() noexcept;
        [[nodiscard]] static std::size_t layer_count() noexcept;
        /** The tables the package owns, in the order a reader should meet them. */
        [[nodiscard]] static const PublishedTable *tables() noexcept;
        [[nodiscard]] static std::size_t table_count() noexcept;
    };
}  // namespace ghostlock::contract
