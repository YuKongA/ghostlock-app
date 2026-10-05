#ifndef GHOSTLOCK_PROFILE_ENTRY_H
#define GHOSTLOCK_PROFILE_ENTRY_H

/* Profile entry points: the App path (stdin) and the prebuilt-profile path
 * (file). Both frame the typed transport into the neutral profile::Document
 * (GLKv3 first, v2 still accepted); no JSON or legacy rules reach this unit and
 * no owner is bound here. */

#include "profile/document.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ghostlock::profile_entry {
    struct ReadResult final {
        profile::Document document;
        /* S4 R4: the framed bytes a String field's std::string_view aliases.
         * Heap-owned so the address is stable across the move that returns the
         * result, and it outlives the Document/View it is viewed through. Null
         * only for an empty/failed read. */
        std::unique_ptr<std::string> storage;
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
