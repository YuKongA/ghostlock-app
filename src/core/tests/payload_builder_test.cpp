#include "memory/payload_builder.h"

#include <stdio.h>

int main(void) {
  if (!payload_builder_fixed_vector_test()) {
    fputs("payload builder fixed-vector comparison failed\n", stderr);
    return 1;
  }
  puts("payload builder request/layout vectors passed");
  return 0;
}
