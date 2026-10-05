#ifndef GHOSTLOCK_BOOTIMG_PHYSMAP_H
#define GHOSTLOCK_BOOTIMG_PHYSMAP_H

#include <cstdint>
#include <span>
#include <optional>
#include <string>
#include <stdexcept>

namespace ghostlock::bootimg {

    /* vr.ko probe outcome: the per-task tag offsets carried by the module inside
     * a vendor_boot image's ramdisk. */
    struct VrKoProbeResult {
        std::uint64_t tag_a_offset = 0;
        std::uint64_t tag_b_offset = 0; // 通常为 0x2c
        bool valid = false;
        std::string module_path;
        std::string error_msg;
    };

    // --- 异常类：用于强制检查失败 ---
    class VendorBootRequiredError : public std::runtime_error {
    public:
        explicit VendorBootRequiredError(const std::string& msg) : std::runtime_error(msg) {}
    };

    // --- 设备品牌检测 ---
    /** Scans the boot image bytes for a vendor marker (fallback path). */
    [[nodiscard]] std::string detect_device_brand(std::span<const std::uint8_t> boot_img);

    /**
     * Primary brand detection: reads the Android system properties
     * (ro.product.manufacturer / brand / vendor+odm partitions), which is what
     * the device actually reports, and only falls back to scanning the boot
     * image bytes when no property matches. Returns "vivo", "iqoo" or
     * "unknown" (lower case).
     */
    [[nodiscard]] std::string detect_device_brand_runtime(std::span<const std::uint8_t> boot_img);
    
    /* Probe the vr.ko module inside a vendor_boot image for its per-task tag
     * offsets.
     *
     * Always returns a result: `valid` says whether the offsets were found, and
     * `error_msg` says which step failed when they were not. A bare "no result"
     * would leave both the log and the user without a reason, and the reasons are
     * actionable (wrong container, missing module, unknown module variant). */
    [[nodiscard]] VrKoProbeResult
    probe_vrko_from_vendor_boot(std::span<const std::uint8_t> vendor_boot_img);

    /* Kernel physical load base read from a UEFI firmware image.
     *
     * The UEFI memory map prints the kernel region as
     * `0x<base>, 0x<size>, "Kernel"`, so the base is the pair immediately before
     * the label. Exactly one distinct base must be present; zero or several
     * yield nullopt instead of a guess. This mirrors
     * tools/extract_rs/src/fdt.rs::find_kernel_memory_map_entry — a profile's
     * `kernel_phys_load` is produced by that same derivation, which is what
     * makes a runtime cross-check against it meaningful.
     *
     * MediaTek devices carry the equivalent value inside xbl_config.img as an
     * FDT blob; that derivation stays in the Rust extractor, which owns the
     * format and is what the app runs when the user attaches the file. */
    [[nodiscard]] std::optional<std::uint64_t>
    kernel_phys_load_from_uefi(std::span<const std::uint8_t> uefi_img);

} // namespace ghostlock::bootimg

#endif // GHOSTLOCK_BOOTIMG_PHYSMAP_H
