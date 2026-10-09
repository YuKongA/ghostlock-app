/** @file contract/contract.cpp
 *  The contract package's face, read out of the tables that own the numbers.
 */
#include "contract/contract.hpp"

namespace ghostlock::contract {
    const PublishedTable *Structure::layers() noexcept {
        static const PublishedTable rows[kLayerCount] = {
            {"contract", 0U}, {"support", 0U}, {"memory", 0U},  {"backend", 0U},
            {"pipeline", 0U}, {"terminal", 0U}, {"script", 0U}, {"session", 0U},
            {"profile", 0U},
        };
        /* The names are a second spelling of kLayers, so they are checked against it
         *  here rather than trusted: a layer renamed in one place and not the other
         *  would otherwise be two answers to one question. */
        static_assert(sizeof(rows) / sizeof(rows[0]) == kLayerCount,
                      "the published layer list must have the catalogue's length");
        for (std::size_t i = 0U; i < kLayerCount; ++i) {
            const std::string_view published(rows[i].name);
            const std::string_view declared(kLayers[i].name);
            if (published.size() != declared.size() || published != declared) {
                return nullptr;
            }
        }
        return rows;
    }

    std::size_t Structure::layer_count() noexcept { return kLayerCount; }

    const PublishedTable *Structure::tables() noexcept {
        static const PublishedTable rows[] = {
            {"kChainCatalog", kChainCount},
            {"kOpCatalog", kOpCount},
            {"kKmiCatalog", kKmiCount},
        };
        return rows;
    }

    std::size_t Structure::table_count() noexcept { return 3U; }
}  // namespace ghostlock::contract
