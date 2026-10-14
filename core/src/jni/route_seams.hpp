#pragma once
/** @file jni/route_seams.hpp
 *  The ten helpers the pselect route body called, resolved one by one.
 *
 *  Source: bddfea4:src/core/fops.c and util.c. The C route reached them through file scope,
 *  like everything else; here each one is either a seam (a function pointer the caller fills)
 *  or gone, with the reason written next to it. A body cannot be moved before its helpers
 *  have a landing spot -- that is the mistake this file exists to avoid.
 *
 *  Kept as seams (they must stay C side: syscalls, kernel interaction, or fd manipulation):
 *    prepare_good_kernel_page   util.c:684   prepares the kernel page, fills SprayProducts
 *    open_selected_fds          fops.c       opens the fds the fdsets name
 *    reserve_standard_io        util.c       the route may replace low fds, including stdout
 *    restore_standard_io        util.c       undo the above
 *
 *  Resolved into parameters (nothing left to call):
 *    compact_timeout_values     produced two numbers; they are PselectTuning fields now
 *    route_delay_usec           produced a per-attempt delay; a tuning field now
 *    pselect_words_per_set      pure arithmetic ((NFDS + 63) / 64); not moved
 *
 *  Kept but trivial (pure readers, no state):
 *    fdset_put_word / fdset_get_word   word access into an fd_set
 *    prepare_pselect_fdsets            fills a word table; takes SprayProducts + OffsetsView
 *    fops_elapsed_ms                   elapsed-ms since a start time; a timestamp reader
 *
 *  log_sync stays where it is: it belongs to the log verb, not to this route.
 */
#include <cstdint>

#include "jni/route_common.hpp"
#include "jni/race_threads.hpp"

namespace ghostlock::jni {
    /** The helpers that must run C side, as seams the caller fills. */
    struct RouteSeams final {
        /** Prepare the kernel page (util.c:684). Fills products on success. */
        bool (*prepare_good_kernel_page)(SprayProducts &products, void *ctx) = nullptr;
        /** Open the fds the three fdsets name (fops.c). */
        bool (*open_selected_fds)(const void *in, const void *out, const void *ex, int high_read,
                                  int pipe_write, void *ctx) = nullptr;
        /** Keep the route from losing stdout/stderr when it replaces low fds. */
        void (*reserve_standard_io)(void *ctx) = nullptr;
        /** Undo reserve_standard_io. */
        void (*restore_standard_io)(void *ctx) = nullptr;
        void *context = nullptr;
    };
}  // namespace ghostlock::jni
