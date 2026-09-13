#include "payload_builder.h"

#include <stdio.h>

int main(void) {
  if (!payload_builder_equivalence_test()) {
    fputs("payload builder fixed-vector comparison failed\n", stderr);
    return 1;
  }
  puts("payload builder fixed-vector comparison passed (5/5)");
  return 0;
}
