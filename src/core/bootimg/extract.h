#ifndef GHOSTLOCK_BOOTIMG_EXTRACT_H
#define GHOSTLOCK_BOOTIMG_EXTRACT_H

#include "support/native_result.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ghostlock::bootimg {

    /* Boot-extraction failures. */
    enum class BootError : int {
        IoError,
        HeaderTooShort,
        UnknownCompression,
        ImageTooSmall,
        ImageHeaderSizeMismatch,
        BadKallsyms,
        BadBtf,
        SymbolNotFound,
        TracepointNotFound,
        VendorBootMissing,
    };

    /* The small set of addresses the vr.ko neutralizer needs from the boot
     * image, resolved from kallsyms and verified via embedded BTF.
     * The exact profile value is sys_exit_tp + offsetof(tracepoint, funcs). */
    struct VrSymbols {
        uintptr_t sys_exit_tp = 0;
        uintptr_t commit_creds_tp = 0;
        uintptr_t probestub_sys_exit = 0;
    };

    /* Resolve the vr.ko-relevant addresses from a raw boot image:
     *   1. parse_header() -> kernel segment [offset, size)
     *   2. validate the arm64 Image size (vmlinux payload)
     *   3. kallsyms token table -> symbol addresses
     *   4. embedded BTF -> offsetof(struct tracepoint, funcs)
     * Partial success is never returned. */
    support::Result<VrSymbols, BootError> extract_vr_symbols(std::span<const std::uint8_t> raw_bootimg);

    /* Convenience: the single profile value vr1 needs. */
    support::Result<uint64_t, BootError> recover_sys_exit_tp_funcs(std::span<const std::uint8_t> raw_bootimg);

    /* Load a raw boot image from a device path. */
    support::Result<std::vector<std::uint8_t>, BootError>
    load_boot_image(const char* boot_path, std::size_t max_read = 64 * 1024 * 1024);


} // namespace ghostlock::bootimg

#endif // GHOSTLOCK_BOOTIMG_EXTRACT_H
