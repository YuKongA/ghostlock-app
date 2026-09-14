#include "memory/heap_context.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static void test_move_preserves_page_as_one_owner(void) {
  HeapContext context;
  heap_context_init(&context);
  assert(context.current.reclaim.fd[0] == -1);
  assert(context.prebuilt.state == PAYLOAD_PAGE_EMPTY);

  int owned[2];
  assert(pipe(owned) == 0);
  context.current.base = 0x12340000;
  context.current.fake_lock = 0x12340100;
  context.current.reclaim.fd[0] = owned[0];
  context.current.reclaim.fd[1] = owned[1];
  context.current.state = PAYLOAD_PAGE_CURRENT;

  assert(payload_page_move(&context.prebuilt, &context.current,
                           PAYLOAD_PAGE_PREBUILT));
  assert(context.current.state == PAYLOAD_PAGE_EMPTY);
  assert(context.current.reclaim.fd[0] == -1);
  assert(context.prebuilt.base == 0x12340000);
  assert(context.prebuilt.fake_lock == 0x12340100);
  assert(context.prebuilt.state == PAYLOAD_PAGE_PREBUILT);

  payload_page_destroy(&context.prebuilt);
  errno = 0;
  assert(fcntl(owned[0], F_GETFD) == -1 && errno == EBADF);
}

static void test_move_rejects_partial_or_occupied_ownership(void) {
  HeapContext context;
  heap_context_init(&context);
  context.current.reclaim.fd[0] = 3;
  context.current.state = PAYLOAD_PAGE_CURRENT;
  assert(!payload_page_move(&context.prebuilt, &context.current,
                            PAYLOAD_PAGE_PREBUILT));

  context.current.reclaim.fd[1] = 4;
  context.prebuilt.state = PAYLOAD_PAGE_PREBUILT;
  assert(!payload_page_move(&context.prebuilt, &context.current,
                            PAYLOAD_PAGE_PREBUILT));
  context.current.reclaim.fd[0] = -1;
  context.current.reclaim.fd[1] = -1;
}

int main(void) {
  test_move_preserves_page_as_one_owner();
  test_move_rejects_partial_or_occupied_ownership();
  puts("heap_context_test: ok");
  return 0;
}
