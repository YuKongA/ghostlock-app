#ifndef LSPRIVILEGE_PROFILE_ENTRY_H
#define LSPRIVILEGE_PROFILE_ENTRY_H

#include <cstddef>

#include "profile/binary.h"
#include "profile/model.h"

namespace lsprivilege::profile_entry {
    /* Reads a typed document from stdin (binary-safe, EOF-terminated, 1 MiB
     * cap) and decodes it into the transport struct. `ids`, when given,
     * receives the decoded component selection (Batch 3.1). */
    int32_t read_priv1_stdin(struct lsprivilege::profile::kernel_offsets *out, char *release_buf,
                        size_t release_buf_cap,
                        lsprivilege::binary_profile::component_ids *ids = nullptr);

    /* Reads a length-prefixed typed document from stdin (4-byte big-endian
     * length + payload). Unlike read_priv1_stdin it does not consume to EOF, so
     * stdin stays usable for the status-record ACK channel. */
    int32_t read_priv1_frame_stdin(struct lsprivilege::profile::kernel_offsets *out, char *release_buf,
                        size_t release_buf_cap,
                        lsprivilege::binary_profile::component_ids *ids = nullptr);

    /* Reads a typed document from a file path. */
    int32_t read_priv1_file(const char *path, struct lsprivilege::profile::kernel_offsets *out,
                       char *release_buf, size_t release_buf_cap,
                       lsprivilege::binary_profile::component_ids *ids = nullptr);
} // namespace lsprivilege::profile_entry

#endif
