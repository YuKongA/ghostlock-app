#ifndef GHOSTLOCK_PROFILE_ENTRY_H
#define GHOSTLOCK_PROFILE_ENTRY_H

#include <cstddef>

#include "profile/binary.h"
#include "profile/model.h"

namespace ghostlock::profile_entry {
    /* Reads a typed document from stdin (binary-safe, EOF-terminated, 1 MiB
     * cap) and decodes it into the transport struct. `ids`, when given,
     * receives the decoded component selection (Batch 3.1). */
    int32_t read_glk1_stdin(struct ghostlock::profile::kernel_offsets *out, char *release_buf,
                        size_t release_buf_cap,
                        ghostlock::binary_profile::component_ids *ids = nullptr);

    /* Reads a typed document from a file path. */
    int32_t read_glk1_file(const char *path, struct ghostlock::profile::kernel_offsets *out,
                       char *release_buf, size_t release_buf_cap,
                       ghostlock::binary_profile::component_ids *ids = nullptr);
} // namespace ghostlock::profile_entry

#endif
