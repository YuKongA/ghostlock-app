#include "physmap.h"
#include "lz4_legacy.h"
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <cctype>
#include <cstdlib>

namespace ghostlock::bootimg {

    static bool is_hex(std::uint8_t c) noexcept {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    }

    static std::uint8_t hex_value(std::uint8_t c) noexcept {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        return static_cast<std::uint8_t>(c - 'A' + 10);
    }

    /* cpio newc fields are fixed 8-character hex, and the field that follows is
     * hex too -- a variable-width parse would swallow its digits. */
    static bool parse_hex8(const std::uint8_t *at, std::uint32_t &out) noexcept {
        std::uint32_t value = 0;
        for (std::size_t k = 0; k < 8; ++k) {
            if (!is_hex(at[k])) return false;
            value = (value << 4) | hex_value(at[k]);
        }
        out = value;
        return true;
    }


    // --- 1. 设备品牌检测 ---
    std::string detect_device_brand(std::span<const std::uint8_t> boot_img) {
        // 在 boot_img 的前 1MB 中搜索 vivo/iQOO
        std::size_t search_limit = std::min(boot_img.size(), static_cast<std::size_t>(1024 * 1024));
        
        for (std::size_t i = 0; i + 4 < search_limit; ++i) {
            if (std::strncmp(reinterpret_cast<const char*>(boot_img.data() + i), "vivo", 4) == 0) {
                return "vivo";
            }
            if (std::strncmp(reinterpret_cast<const char*>(boot_img.data() + i), "iqoo", 4) == 0) {
                return "iqoo";
            }
        }
        return "unknown";
    }

    /* Android system properties: the authoritative brand source. Guarded so the
     * host unit tests (non-Android) still build. */
#if defined(__ANDROID__)
#include <sys/system_properties.h>
    static std::string read_property(const char *name) {
        char value[PROP_VALUE_MAX] = {};
        const int len = __system_property_get(name, value);
        return len > 0 ? std::string(value, static_cast<std::size_t>(len)) : std::string();
    }
#else
    static std::string read_property(const char *) { return {}; }
#endif

    std::string detect_device_brand_runtime(std::span<const std::uint8_t> boot_img) {
        static const char *kBrandProps[] = {
            "ro.product.manufacturer",
            "ro.product.brand",
            "ro.product.vendor.manufacturer",
            "ro.product.odm.manufacturer",
            "ro.product.system.manufacturer",
            "ro.vivo.os.version",
            nullptr,
        };
        for (int32_t i = 0; kBrandProps[i]; ++i) {
            std::string value = read_property(kBrandProps[i]);
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (value.find("iqoo") != std::string::npos) return "iqoo";
            if (value.find("vivo") != std::string::npos) return "vivo";
        }
        return detect_device_brand(boot_img);
    }

    // --- 2. VRKO 探测 (完整实现，含 LZ4-legacy 解压) ---
    VrKoProbeResult probe_vrko_from_vendor_boot(std::span<const std::uint8_t> vendor_boot_img) {
        VrKoProbeResult res;
        const auto fail = [&res](const char *why) -> VrKoProbeResult {
            res.valid = false;
            res.error_msg = why;
            return res;
        };

        if (vendor_boot_img.size() < 2124) {
            return fail("file is smaller than a vendor_boot header");
        }
        const char *magic = reinterpret_cast<const char *>(vendor_boot_img.data());
        if (std::strncmp(magic, "VNDRBOOT", 8) != 0) {
            return fail("not a VNDRBOOT image (boot.img and init_boot.img are different partitions)");
        }

        std::uint32_t header_version = 0;
        std::memcpy(&header_version, vendor_boot_img.data() + 8, sizeof(header_version));
        if (header_version < 3 || header_version > 4) {
            return fail("unsupported vendor_boot header version (only v3/v4 carry a ramdisk)");
        }
        std::uint32_t page_size = 0;
        std::memcpy(&page_size, vendor_boot_img.data() + 12, sizeof(page_size));
        if (page_size == 0 || page_size > 65536) {
            return fail("implausible page size in the vendor_boot header");
        }
        std::uint32_t ramdisk_size = 0;
        std::memcpy(&ramdisk_size, vendor_boot_img.data() + 24, sizeof(ramdisk_size));
        if (ramdisk_size == 0) {
            return fail("the image carries no vendor_ramdisk");
        }
        std::uint32_t header_size = 0;
        std::memcpy(&header_size, vendor_boot_img.data() + 2096, sizeof(header_size));
        if (header_size == 0) header_size = page_size;

        const std::size_t ramdisk_start =
            (static_cast<std::size_t>(header_size) + page_size - 1) / page_size * page_size;
        if (ramdisk_start + ramdisk_size > vendor_boot_img.size()) {
            return fail("the declared ramdisk runs past the end of the file (truncated image?)");
        }
        const std::span<const std::uint8_t> ramdisk_raw(
            vendor_boot_img.data() + ramdisk_start, ramdisk_size);

        const std::vector<std::uint8_t> ramdisk_vec =
            lz4_decompress_legacy(ramdisk_raw, 512u * 1024u * 1024u);
        if (ramdisk_vec.empty()) {
            return fail("vendor_ramdisk is not a decodable LZ4-legacy stream");
        }
        const std::span<const std::uint8_t> ramdisk(ramdisk_vec.data(), ramdisk_vec.size());

        /* cpio newc walk. Only the module entry is retained. */
        std::vector<std::tuple<std::string, std::size_t, std::size_t>> entries;
        std::size_t i = 0;
        while (i + 110 <= ramdisk.size()) {
            if (std::memcmp(ramdisk.data() + i, "070701", 6) != 0) break;
            std::uint32_t filesize = 0;
            std::uint32_t namesize = 0;
            if (!parse_hex8(ramdisk.data() + i + 54, filesize) ||
                !parse_hex8(ramdisk.data() + i + 94, namesize)) {
                break;
            }
            if (namesize == 0 || namesize > 4096) break;

            const std::size_t name_start = i + 110;
            if (name_start + namesize > ramdisk.size()) break;
            const std::string name(
                reinterpret_cast<const char *>(ramdisk.data() + name_start), namesize - 1);
            if (name == "TRAILER!!!") break;

            const std::size_t data_off = (name_start + namesize + 3u) & ~std::size_t{3};
            const std::size_t next = (data_off + filesize + 3u) & ~std::size_t{3};
            if (data_off + filesize > ramdisk.size()) break;
            if (name.rfind("lib/modules/", 0) == 0 && name.find("/vr.ko") != std::string::npos) {
                entries.emplace_back(name, data_off, filesize);
            }
            i = next;
        }
        if (entries.empty()) {
            return fail("no lib/modules/vr.ko inside the vendor_ramdisk "
                        "(this device may not ship the anti-root module)");
        }

        /* The flat path is what modprobe resolves for a full `<uname -r>` name. */
        auto chosen = entries.front();
        for (const auto &entry : entries) {
            if (std::get<0>(entry) == "lib/modules/vr.ko") {
                chosen = entry;
                break;
            }
        }
        res.module_path = std::get<0>(chosen);
        const std::span<const std::uint8_t> ko(
            ramdisk.data() + std::get<1>(chosen), std::get<2>(chosen));

        if (ko.size() < 64 || ko[0] != 0x7f || ko[1] != 'E' || ko[2] != 'L' || ko[3] != 'F') {
            return fail("the module extracted from the ramdisk is not an ELF object");
        }
        std::uint64_t shoff = 0;
        std::uint16_t shentsize = 0;
        std::uint16_t shnum = 0;
        std::memcpy(&shoff, ko.data() + 0x28, sizeof(shoff));
        std::memcpy(&shentsize, ko.data() + 0x3a, sizeof(shentsize));
        std::memcpy(&shnum, ko.data() + 0x3c, sizeof(shnum));
        if (shoff == 0 || shentsize < 64 || shnum == 0) {
            return fail("the module has no usable ELF section table");
        }

        /* The tag code is one `mrs xN, sp_el0` followed by byte accesses through
         * that same register: the first immediate is tag A, the next different
         * one is tag B. The window is 8 instructions, which is what the three
         * measured families fit into. */
        for (std::uint16_t s = 0; s < shnum; ++s) {
            const std::size_t off =
                static_cast<std::size_t>(shoff) + static_cast<std::size_t>(s) * shentsize;
            if (off + 64 > ko.size()) break;
            std::uint32_t type = 0;
            std::uint64_t flags = 0;
            std::uint64_t sect_off = 0;
            std::uint64_t sect_size = 0;
            std::memcpy(&type, ko.data() + off + 4, sizeof(type));
            std::memcpy(&flags, ko.data() + off + 8, sizeof(flags));
            std::memcpy(&sect_off, ko.data() + off + 0x18, sizeof(sect_off));
            std::memcpy(&sect_size, ko.data() + off + 0x20, sizeof(sect_size));
            if (type != 1 || (flags & 0x4u) == 0 || sect_size == 0) continue;
            if (sect_off + sect_size > ko.size()) continue;
            const std::span<const std::uint8_t> section(ko.data() + sect_off, sect_size);

            for (std::size_t j = 0; j + 4 <= section.size(); j += 4) {
                std::uint32_t insn = 0;
                std::memcpy(&insn, section.data() + j, sizeof(insn));
                if ((insn & 0xFFFFFFE0u) != 0xD5384100u) continue; /* mrs xN, sp_el0 */
                const std::uint32_t task_reg = insn & 0x1Fu;

                std::uint32_t tag_a = 0;
                std::uint32_t tag_b = 0;
                for (std::size_t k = 1; k < 8 && j + k * 4 + 4 <= section.size(); ++k) {
                    std::uint32_t next_insn = 0;
                    std::memcpy(&next_insn, section.data() + j + k * 4, sizeof(next_insn));
                    const std::uint32_t op = next_insn & 0xFFC00000u;
                    if (op != 0x39000000u && op != 0x39400000u) continue; /* strb / ldrb */
                    if (((next_insn >> 5) & 0x1Fu) != task_reg) continue;
                    const std::uint32_t imm = (next_insn >> 10) & 0xFFFu;
                    if (imm == 0 || imm > 255) continue;
                    if (tag_a == 0) {
                        tag_a = imm;
                    } else if (tag_b == 0 && imm != tag_a) {
                        tag_b = imm;
                        break;
                    }
                }
                if (tag_a != 0 && tag_b != 0) {
                    res.tag_a_offset = tag_a;
                    res.tag_b_offset = tag_b;
                    res.valid = true;
                    return res;
                }
            }
        }
        return fail("the module carries no recognisable tag code "
                    "(this vr.ko variant has not been measured)");
    }


    /* UEFI memory-map scan. The map is textual in the firmware image:
     *     0x0000000080000000, 0x0000000002000000, "Kernel"
     * The label's line ends with the size, so the base is the *second* hex group
     * walking left. Several distinct bases mean the image carries maps for more
     * than one target and picking one would be a guess — that is reported as "no
     * answer" instead. */
    std::optional<std::uint64_t>
    kernel_phys_load_from_uefi(std::span<const std::uint8_t> uefi_img) {
        static constexpr char kLabel[] = "\"Kernel\"";
        static constexpr std::size_t kLabelLen = sizeof(kLabel) - 1;
        static constexpr std::size_t kWindow = 96;

        const auto skip_spaces_left = [&](std::size_t floor, std::size_t &at) {
            while (at > floor && (uefi_img[at - 1] == ' ' || uefi_img[at - 1] == '\t')) {
                --at;
            }
        };
        /* Returns the start of a `0x<hex>` group ending at `end`, or npos. */
        const auto hex_group_start = [&](std::size_t floor, std::size_t end) -> std::size_t {
            std::size_t begin = end;
            while (begin > floor && is_hex(uefi_img[begin - 1])) --begin;
            if (begin == end || begin < floor + 2) return std::string::npos;
            if (uefi_img[begin - 1] != 'x' || uefi_img[begin - 2] != '0') {
                return std::string::npos;
            }
            return begin;
        };

        std::vector<std::uint64_t> bases;
        const std::size_t size = uefi_img.size();
        for (std::size_t i = 0; i + kLabelLen <= size; ++i) {
            if (std::memcmp(uefi_img.data() + i, kLabel, kLabelLen) != 0) continue;

            const std::size_t floor = i > kWindow ? i - kWindow : 0;
            std::size_t pos = i;
            skip_spaces_left(floor, pos);
            if (pos == 0 || uefi_img[pos - 1] != ',') continue;

            /* size group */
            --pos;
            skip_spaces_left(floor, pos);
            const std::size_t size_start = hex_group_start(floor, pos);
            if (size_start == std::string::npos) continue;

            /* base group, two separators further left */
            pos = size_start - 2;
            skip_spaces_left(floor, pos);
            if (pos == 0 || uefi_img[pos - 1] != ',') continue;
            --pos;
            skip_spaces_left(floor, pos);
            const std::size_t base_end = pos;
            const std::size_t base_start = hex_group_start(floor, base_end);
            if (base_start == std::string::npos) continue;

            std::uint64_t base = 0;
            bool overflow = false;
            for (std::size_t k = base_start; k < base_end; ++k) {
                const auto digit = static_cast<std::uint64_t>(hex_value(uefi_img[k]));
                if (base > (UINT64_MAX - digit) / 16u) {
                    overflow = true;
                    break;
                }
                base = base * 16u + digit;
            }
            if (overflow || base == 0) continue;
            bases.push_back(base);
        }

        std::sort(bases.begin(), bases.end());
        bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
        if (bases.size() != 1) return std::nullopt;
        return bases.front();
    }

} // namespace ghostlock::bootimg
