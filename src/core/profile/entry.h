#ifndef GHOSTLOCK_PROFILE_ENTRY_H
#define GHOSTLOCK_PROFILE_ENTRY_H

/* Profile entry points: the App path (stdin) and the prebuilt-profile path
 * (file). Both frame the typed transport into the neutral profile::Document
 * (GLKv3 first, v2 still accepted); no JSON or legacy rules reach this unit and
 * no owner is bound here. */

#include "profile/document.hpp"

#include <cstdint>

namespace ghostlock::profile_entry {
    struct ReadResult final {
        profile::Document document;
        /* 0 = ok; non-zero keeps the historical errno behaviour. */
        int32_t error = 0;
    };

    /* Reads a typed document from stdin (binary-safe, EOF-terminated, 1 MiB
     * cap). */
    ReadResult read_glk1_stdin();

    /* Reads a length-prefixed typed document from stdin (4-byte big-endian
     * length + payload). Unlike read_glk1_stdin it does not consume to EOF, so
     * stdin stays usable for the status-record ACK channel. */
    ReadResult read_glk1_frame_stdin();

    /* Reads a typed document from a file path. */
    ReadResult read_glk1_file(const char *path);
} // namespace ghostlock::profile_entry

#endif
