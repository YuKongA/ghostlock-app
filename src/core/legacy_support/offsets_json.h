#ifndef OFFSETS_JSON_H
#define OFFSETS_JSON_H

#include <cstddef>

#include <string_view>

#include "profile.h"

namespace ghostlock {

/* Decode one resolved JSON profile. The entrypoints own format selection and
 * decoding; this stays for assets/imports and host tests. */
int load_resolved_profile_json(const char *path, struct ghostlock::profile::kernel_offsets *out,
                               char *release_buf, size_t release_buf_cap);

/* JSON lexical helpers shared with legacy_support. `select_entry` finds the
 * entry whose "release" equals `release` in a top-level array (or accepts a
 * single object as-is); `fill_entry` fills *out from one entry object. */
namespace profile_json {
    int select_entry(std::string_view document, const char *release,
                     std::string_view *entry_out);

    void fill_entry(struct ghostlock::profile::kernel_offsets *out, const char *release_buf,
                    std::string_view object);
} // namespace profile_json

} // namespace ghostlock

#endif
