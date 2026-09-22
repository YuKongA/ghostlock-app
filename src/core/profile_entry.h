#ifndef GHOSTLOCK_PROFILE_ENTRY_H
#define GHOSTLOCK_PROFILE_ENTRY_H

#include <stddef.h>

#include "profile.h"

namespace ghostlock::profile_entry {

/* Reads a GLK1 v2 document from stdin (binary-safe, EOF-terminated, 1 MiB
 * cap) and decodes it into the transport struct. */
int read_glk1_stdin(struct kernel_offsets *out, char *release_buf,
        size_t release_buf_cap);

/* Reads a GLK1 v2 document from a file path. */
int read_glk1_file(const char *path, struct kernel_offsets *out,
        char *release_buf, size_t release_buf_cap);

}  // namespace ghostlock::profile_entry

#endif
