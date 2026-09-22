#ifndef GHOSTLOCK_LEGACY_ENTRYPOINT_STARTER_H
#define GHOSTLOCK_LEGACY_ENTRYPOINT_STARTER_H

#include <cstddef>

#include "profile.h"

namespace ghostlock::legacy_support {
    /* No-argument entrypoint: reads uname -r and <GHOSTLOCK_HOME>/offsets.json,
 * converts the matching entry and returns the transport struct. Mirrors the
 * remote/main invocation contract (no argv parsing). */
    int start_legacy_entrypoint(struct ghostlock::profile::kernel_offsets *out, char *release_buf,
                                size_t release_buf_cap);
} // namespace ghostlock::legacy_support

#endif
