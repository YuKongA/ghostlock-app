#ifndef OFFSETS_JSON_H
#define OFFSETS_JSON_H

#include <stddef.h>
#include "profile.h"

/* Decode one fully resolved runtime profile. Configuration-source selection
 * and merging belong to Kotlin; Native performs defensive validation. */
int load_resolved_profile_json(const char *path, struct kernel_offsets *out,
                               char *release_buf, size_t release_buf_cap);

#endif
