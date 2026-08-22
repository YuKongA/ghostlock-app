#ifndef OFFSETS_JSON_H
#define OFFSETS_JSON_H

#include <stddef.h>
#include "../kernels/offsets.h"

/* Load a kernel table from a JSON file (single object or array of objects)
 * and fill `out` for the kernel whose "release" equals `release`.  The format
 * matches tools/extract_rs/ghostlock-extract --format json, plus the
 * pselect_waiter_shift, compact_waiter and mm_struct_sz fields (the extractor
 * emits the latter two for 6.1 entries only).  Fields absent from the JSON
 * keep the values already in *out (zero it for a fresh table, or seed it
 * with a built-in entry the JSON is overriding).  Returns 0 and sets *out on
 * match; -1 when no entry matches or the file is unreadable/malformed.
 * release_buf receives a copy of the matched release string that stays valid
 * after the call. */
int load_offsets_json(const char *path, const char *release,
                      struct kernel_offsets *out, char *release_buf,
                      size_t release_buf_cap);

#endif
