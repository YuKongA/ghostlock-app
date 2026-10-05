/* CM-2 negative fixture: a shared object with no glk_entry symbol at all.
 * The loader must reject it at the symbol probe and release the handle. */

#include <stdint.h>

void glk_not_entry(void) {}
