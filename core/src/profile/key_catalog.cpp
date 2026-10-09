/** @file profile/key_catalog.cpp
 *  Writing the manifest out of the table that owns it.
 *
 *  The format is fixed: the header comments first, then one row per key with eight
 *  tab-separated columns in the order the table declares them. Nothing here decides
 *  anything -- what to write comes from the table, and this only writes it.
 */
#include "profile/key_catalog.hpp"

namespace ghostlock::profile {
    bool write_manifest(std::FILE *out) noexcept {
        if (out == nullptr) {
            return false;
        }
        for (std::size_t i = 0U; i < kManifestHeaderCount; ++i) {
            if (std::fprintf(out, "%s\n", kManifestHeader[i].data()) < 0) {
                return false;
            }
        }
        for (std::size_t i = 0U; i < kKeyRowCount; ++i) {
            const KeyRow &row = kKeyRows[i];
            const int written = std::fprintf(out, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n",
                                            row.owner.data(), row.path.data(), row.wire.data(),
                                            row.required.data(), row.by_default.data(),
                                            row.source.data(), row.doc.data(), row.width.data());
            if (written < 0) {
                return false;
            }
        }
        return true;
    }
}  // namespace ghostlock::profile
