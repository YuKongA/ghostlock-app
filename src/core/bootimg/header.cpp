#include "bootimg/header.h"

#include <cstring>

namespace ghostlock::bootimg {
    /* Magic bytes: "ANDROID!" */
    static constexpr std::uint32_t kBootMagic = 0x00414E44494F5221ULL; // little-endian "ANDROID!"

    static std::uint32_t rd32(const std::uint8_t *p) noexcept {
        std::uint32_t v;
        std::memcpy(&v, p, 4);
        return v;
    }

    static std::uint64_t rd64(const std::uint8_t *p) noexcept {
        std::uint64_t v;
        std::memcpy(&v, p, 8);
        return v;
    }

    /* Common prefix shared by v2/v3:
     * 0x00 magic, 0x08 page_size, 0x0c header_version, 0x10 header_size. */
    bool parse_header(std::span<const std::uint8_t> raw,
                      Header &out,
                      KernelSegment &segment_out) {
        out = Header{};
        if (raw.size() < 0x2000) {
            return false;
        }
        const auto *b = raw.data();
        std::uint64_t magic = rd64(b);
        if (magic != kBootMagic) {
            return false;
        }
        out.page_size = rd32(b + 0x08);
        out.header_size = rd32(b + 0x10);
        if (out.page_size == 0 || out.page_size > 0x400000) {
            return false;
        }

        if (out.header_size >= 0x100) {
            /* v2/v3: kernel_size @ 0x1c? No -- actual AOSP v2 layout:
             * 0x14 kernel_load, 0x18 kernel_addr, 0x1c kernel_size,
             * 0x20 ramdisk_load, 0x24 ramdisk_addr, 0x28 ramdisk_size,
             * 0x2c second_load, 0x30 second_addr, 0x34 second_size,
             * 0x38 tags_addr, 0x3c kernel_cmdline, 0x80 image_version.
             * v4 (>=0x180): kernel_load @0x14, kernel_size @0x1c (32-bit),
             * ramdisk at 0x20/0x24/0x28, ... extra @ 0xc4.
             * Both share kernel_size @ 0x1c. */
            out.kernel_size = rd32(b + 0x1c);
            out.ramdisk_size = rd32(b + 0x28);

            const std::uint32_t hvers = rd32(b + 0x0c);
            out.version = hvers;
            if (hvers < 2) {
                return false;
            }

            if (hvers >= 4 && out.header_size >= 0x180) {
                /* v4: all sizes are 32-bit at the same offsets;
                 * kernel_load_size is redefined but the kernel_size slot
                 * already holds the byte size of the kernel image. */
                out.kernel_load_size = out.kernel_size;
                out.ramdisk_load_size = out.ramdisk_size;
                out.signature_size = rd32(b + 0x58);
            }

            /* Segment layout: each segment follows the header in order.
             * Page-align offsets; a zero-size segment is omitted entirely
             * in AOSP mkbootimg (it contributes 0 bytes, not a page gap). */
            std::size_t off = (out.header_size + out.page_size - 1) &
                              ~(std::size_t)(out.page_size - 1);
            if (out.kernel_size > 0) {
                segment_out.offset = off;
                segment_out.size = out.kernel_size;
                off += (out.kernel_size + out.page_size - 1) &
                       ~(std::size_t)(out.page_size - 1);
            } else {
                segment_out = KernelSegment{};
            }
            return segment_out.size > 0;
        }
        return false;
    }

    Arm64ImageHeader parse_arm64_image(std::span<const std::uint8_t> image) {
        Arm64ImageHeader out{};
        if (image.size() < 0x40) {
            return out;
        }
        /* arm64 Image head starts with "ARM\x64" at offset 0x38 in the
         * struct, but the first byte is actually at offset 0 with the
         * string "ARM\0". The kernel Image on disk starts with these
         * ASCII chars in `head[0..3] == "ARM\0"` followed by the actual
         * ELF-ish header at +0x10 (type) and sizes. */
        auto *b = image.data();
        const bool magic_ok =
            b[0] == 'A' && b[1] == 'R' && b[2] == 'M' && b[3] == 0;
        if (!magic_ok) {
            return out;
        }
        out.image_size = rd64(b + 0x10);
        out.valid = out.image_size > 0 && (out.image_size % 0x1000) == 0;
        return out;
    }
} // namespace ghostlock::bootimg
