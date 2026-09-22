#ifndef GHOSTLOCK_LEGACY_PROFILE_CONVERTER_H
#define GHOSTLOCK_LEGACY_PROFILE_CONVERTER_H

#include <cstddef>

#include "profile.h"

namespace ghostlock::legacy {
    /* Reads a remote/main-era offsets.json (top-level array, release-matched, no
 * schema_version) and converts the matching entry into the native transport
 * struct. A document without an explicit route gets one by legacy geometry
 * inference, so the downstream path always consumes a resolved route. */
    int convert_legacy_offsets(const char *path, const char *release,
                               struct ghostlock::profile::kernel_offsets *out, char *release_buf, size_t release_buf_cap);
} // namespace ghostlock::legacy

#endif
