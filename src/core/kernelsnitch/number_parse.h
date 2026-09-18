#ifndef GHOSTLOCK_NUMBER_PARSE_H
#define GHOSTLOCK_NUMBER_PARSE_H

/* Pure numeric text parsing shared by the KernelSnitch utilities.
 *
 * This header depends on libc only, so the same parser can be locked by host
 * fixed-vector tests (see tests/number_parse_test.cpp). It preserves the
 * legacy strtoul contract exactly: base 0 auto-detects 0x/0 prefixes, leading
 * whitespace and an optional sign are accepted, and overflow reports ERANGE
 * with ULONG_MAX. */

#include <errno.h>
#include <stdlib.h>

struct ParsedUnsigned {
    unsigned long value;
    int valid;
};

/* Parse `text` with the given base. `valid` keeps the legacy condition
 * (errno == 0 && consumed to the end); an empty string therefore still counts
 * as valid zero, matching the pre-migration helper. */
static inline struct ParsedUnsigned number_parse_unsigned(const char *text,
        int base) {
    char *end = NULL;
    errno = 0;
    const unsigned long value = strtoul(text, &end, base);
    struct ParsedUnsigned parsed;
    parsed.value = value;
    parsed.valid = (errno == 0 && end && *end == '\0');
    return parsed;
}

#endif
