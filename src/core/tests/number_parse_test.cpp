/* Host fixed-vector test for the shared numeric text parser.
 *
 * Locks the legacy strtoul contract that KernelSnitch's parse helpers expose:
 * base-0 auto-detection, unsigned wraparound, full-string validation and
 * ERANGE overflow reporting. An empty string is the legacy "valid zero" case,
 * even though platforms differ on the errno strtoul reports for it. */

#include "../kernelsnitch/number_parse.h"

#include <cassert>
#include <climits>
#include <cstdio>

int main(void) {
  struct {
    const char *text;
    int base;
    unsigned long value;
    int valid;
  } vectors[] = {
      {"42", 0, 42UL, 1},
      {"0x1f", 0, 31UL, 1},
      {"010", 0, 8UL, 1},
      {"0", 0, 0UL, 1},
      {"  7", 0, 7UL, 1},
      {"+9", 0, 9UL, 1},
      {"1f", 16, 31UL, 1},
      {"0x1f", 16, 31UL, 1},
      {"", 0, 0UL, 1},
      {"12x", 0, 12UL, 0},
      {"x12", 0, 0UL, 0},
      {"-1", 0, ULONG_MAX, 1},
      {"99999999999999999999999999", 0, ULONG_MAX, 0},
  };
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
    const struct ParsedUnsigned parsed =
        number_parse_unsigned(vectors[i].text, vectors[i].base);
    if (parsed.value != vectors[i].value ||
        parsed.valid != vectors[i].valid) {
      fprintf(stderr, "vector %zu ('%s' base=%d) -> value=%lu valid=%d\n", i,
              vectors[i].text, vectors[i].base, parsed.value, parsed.valid);
    }
    assert(parsed.value == vectors[i].value);
    assert(parsed.valid == vectors[i].valid);
  }
  puts("number_parse_test: ok");
  return 0;
}
