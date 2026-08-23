#ifndef OFFSETS_JSON_H
#define OFFSETS_JSON_H

#include <stddef.h>
#include "../kernels/offsets.h"

/* Fill `out` from the JSON entry whose "release" equals `release`, in the
 * format tools/extract_rs/ghostlock-extract --format json writes. Missing
 * fields keep their values; returns 0 on match, -1 otherwise. release_buf
 * gets a copy of the matched release string. */
int load_offsets_json(const char *path, const char *release,
                      struct kernel_offsets *out, char *release_buf,
                      size_t release_buf_cap);

#endif
