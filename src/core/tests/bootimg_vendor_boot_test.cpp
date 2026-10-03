/*
 * vendor_boot vr.ko probe, end to end and offline.
 *
 * The probe is the piece that decides whether the per-task tag clear can use
 * the device's real offsets, so its whole chain is exercised here with a
 * synthetic image built byte by byte:
 *
 *   VNDRBOOT header v4 -> LZ4-legacy ramdisk -> cpio newc -> lib/modules/vr.ko
 *     -> ELF64 executable section -> `mrs x8, sp_el0` + `ldrb w9, [x8, #imm]`
 *
 * The LZ4 stream is written as literal-only blocks, which is a valid LZ4 block
 * sequence (a block may consist of literals alone), so the test needs no
 * compressor and still goes through the real decoder.
 *
 * Both counters are asserted: tag A (0x06, the 6.6/6.12 family) and tag B
 * (0x2c, all three measured families). A regression in the instruction window,
 * the register match or the member arithmetic fails here.
 */
#include "bootimg/physmap.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
    void put_u32(std::vector<std::uint8_t> &out, std::uint32_t value) {
        for (std::size_t i = 0; i < 4; ++i) out.push_back((value >> (8 * i)) & 0xFF);
    }

    void pad_to(std::vector<std::uint8_t> &out, std::size_t alignment) {
        while (out.size() % alignment != 0) out.push_back(0);
    }

    std::string hex8(std::size_t value) {
        char buffer[9] = {0};
        std::snprintf(buffer, sizeof(buffer), "%08zx", value);
        return buffer;
    }

    /* cpio newc entry: 110-byte header + name + align4 + data + align4. */
    void cpio_entry(std::vector<std::uint8_t> &out, const std::string &name,
                    const std::vector<std::uint8_t> &data) {
        const std::string header =
                "070701" +
                hex8(0) +            /* ino */
                hex8(0x81A4) +       /* mode: regular file 0644 */
                hex8(0) + hex8(0) +  /* uid, gid */
                hex8(1) +            /* nlink */
                hex8(0) +            /* mtime */
                hex8(data.size()) +  /* filesize */
                hex8(0) + hex8(0) +  /* devmajor, devminor */
                hex8(0) + hex8(0) +  /* rdevmajor, rdevminor */
                hex8(name.size() + 1) + /* namesize, NUL included */
                hex8(0);             /* check */
        out.insert(out.end(), header.begin(), header.end());
        out.insert(out.end(), name.begin(), name.end());
        out.push_back(0);
        pad_to(out, 4);
        out.insert(out.end(), data.begin(), data.end());
        pad_to(out, 4);
    }

    /* One LZ4 block holding literals only. */
    void lz4_literal_block(std::vector<std::uint8_t> &out,
                           const std::vector<std::uint8_t> &literals) {
        const std::size_t total = literals.size();
        const std::size_t head = total < 15 ? total : 15;
        out.push_back(static_cast<std::uint8_t>(head << 4));
        if (head == 15) {
            std::size_t rest = total - 15;
            while (rest >= 255) {
                out.push_back(255);
                rest -= 255;
            }
            out.push_back(static_cast<std::uint8_t>(rest));
        }
        out.insert(out.end(), literals.begin(), literals.end());
    }

    /* Minimal ELF64 whose only executable section carries the vr.ko tag code. */
    std::vector<std::uint8_t> build_vr_ko() {
        std::vector<std::uint8_t> code;
        put_u32(code, 0xD5384108u);   /* mrs  x8, sp_el0        */
        put_u32(code, 0x39401909u);   /* ldrb w9, [x8, #0x06]   -> tag A */
        put_u32(code, 0x3940B109u);   /* ldrb w9, [x8, #0x2c]   -> tag B */
        put_u32(code, 0xD503201Fu);   /* nop                    */

        constexpr std::size_t kEhdrSize = 64;
        constexpr std::size_t kShdrSize = 64;
        const std::size_t code_off = kEhdrSize;
        const std::size_t shoff = code_off + code.size();

        std::vector<std::uint8_t> elf(kEhdrSize, 0);
        elf[0] = 0x7F; elf[1] = 'E'; elf[2] = 'L'; elf[3] = 'F';
        elf[4] = 2;    /* ELFCLASS64 */
        elf[5] = 1;    /* little endian */
        elf[6] = 1;    /* version */
        /* e_shoff at 0x28, e_shentsize 0x3a, e_shnum 0x3c */
        for (std::size_t i = 0; i < 8; ++i) elf[0x28 + i] = (shoff >> (8 * i)) & 0xFF;
        elf[0x3a] = kShdrSize & 0xFF;
        elf[0x3c] = 1;  /* one section */
        elf.insert(elf.end(), code.begin(), code.end());

        /* shdr: type = PROGBITS(1), flags = SHF_EXECINSTR(0x4), offset, size */
        std::vector<std::uint8_t> sh(kShdrSize, 0);
        for (std::size_t i = 0; i < 4; ++i) sh[4 + i] = (1u >> (8 * i)) & 0xFF;
        for (std::size_t i = 0; i < 8; ++i) sh[8 + i] = (0x4ull >> (8 * i)) & 0xFF;
        for (std::size_t i = 0; i < 8; ++i) sh[0x18 + i] = (code_off >> (8 * i)) & 0xFF;
        for (std::size_t i = 0; i < 8; ++i) sh[0x20 + i] = (code.size() >> (8 * i)) & 0xFF;
        elf.insert(elf.end(), sh.begin(), sh.end());
        return elf;
    }

    /* VNDRBOOT v4 image carrying that module in an LZ4-legacy ramdisk. */
    std::vector<std::uint8_t> build_vendor_boot() {
        std::vector<std::uint8_t> ramdisk;
        cpio_entry(ramdisk, "lib/modules/vr.ko", build_vr_ko());
        cpio_entry(ramdisk, "TRAILER!!!", {});

        std::vector<std::uint8_t> block;
        lz4_literal_block(block, ramdisk);

        std::vector<std::uint8_t> compressed;
        compressed.push_back(0x02);  /* LZ4-legacy magic 0x184C2102 */
        compressed.push_back(0x21);
        compressed.push_back(0x4C);
        compressed.push_back(0x18);
        /* The per-block field is the length of the *compressed* block that
         * follows, not the size it decodes to. */
        put_u32(compressed, static_cast<std::uint32_t>(block.size()));
        compressed.insert(compressed.end(), block.begin(), block.end());
        put_u32(compressed, 0);      /* frame terminator */

        constexpr std::uint32_t kPageSize = 4096;
        constexpr std::uint32_t kHeaderSize = 2112;

        std::vector<std::uint8_t> image(kHeaderSize, 0);
        std::memcpy(image.data(), "VNDRBOOT", 8);
        /* header_version @8, page_size @12, ramdisk_size @24 */
        for (std::size_t i = 0; i < 4; ++i) image[8 + i] = (4u >> (8 * i)) & 0xFF;
        for (std::size_t i = 0; i < 4; ++i) image[12 + i] = (kPageSize >> (8 * i)) & 0xFF;
        const auto ramdisk_size = static_cast<std::uint32_t>(compressed.size());
        for (std::size_t i = 0; i < 4; ++i) image[24 + i] = (ramdisk_size >> (8 * i)) & 0xFF;
        /* header_size @2096 (the field the probe falls back to page_size without) */
        for (std::size_t i = 0; i < 4; ++i) image[2096 + i] = (kHeaderSize >> (8 * i)) & 0xFF;

        pad_to(image, kPageSize);     /* ramdisk starts page-aligned */
        image.insert(image.end(), compressed.begin(), compressed.end());
        return image;
    }
} // namespace

int32_t main(void) {
    /* The container is recognised before anything else. */
    const auto image = build_vendor_boot();
    if (std::memcmp(image.data(), "VNDRBOOT", 8) != 0) {
        puts("bootimg_vendor_boot_test: fixture is not a VNDRBOOT image");
        return 1;
    }

    const auto probed = ghostlock::bootimg::probe_vrko_from_vendor_boot(image);
    if (!probed.valid) {
        printf("bootimg_vendor_boot_test: probe failed: %s\n", probed.error_msg.c_str());
        return 1;
    }
    if (probed.module_path != "lib/modules/vr.ko") {
        printf("bootimg_vendor_boot_test: module path %s\n", probed.module_path.c_str());
        return 1;
    }
    if (probed.tag_a_offset != 0x06 || probed.tag_b_offset != 0x2c) {
        printf("bootimg_vendor_boot_test: offsets A=0x%llx B=0x%llx\n",
               static_cast<unsigned long long>(probed.tag_a_offset),
               static_cast<unsigned long long>(probed.tag_b_offset));
        return 1;
    }

    /* A non-vendor_boot container is refused rather than misparsed. */
    std::vector<std::uint8_t> not_vendor_boot(4096, 0x41);
    if (ghostlock::bootimg::probe_vrko_from_vendor_boot(not_vendor_boot).valid) {
        puts("bootimg_vendor_boot_test: non-VNDRBOOT input accepted");
        return 1;
    }

    puts("bootimg_vendor_boot_test: ok");
    return 0;
}
