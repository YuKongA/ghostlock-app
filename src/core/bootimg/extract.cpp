#include "bootimg/extract.h"
#include "bootimg/header.h"
#include "bootimg/kallsyms.h"
#include "bootimg/btf.h"
#include "lz4_legacy.h"

#include "support/native_result.hpp"

#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <expected>

namespace ghostlock::bootimg {

    // Helper: read file to memory vector
    static support::Result<std::vector<std::uint8_t>, BootError> read_file_to_vec(const char* path) {
        int fd = open(path, O_RDONLY);
        if (fd < 0) return support::Result<std::vector<std::uint8_t>, BootError>{std::unexpected{BootError::IoError}};

        struct stat st{};
        if (fstat(fd, &st) != 0) { close(fd); return support::Result<std::vector<std::uint8_t>, BootError>{std::unexpected{BootError::IoError}}; }

        std::size_t sz = static_cast<std::size_t>(st.st_size);
        void* ptr = mmap(nullptr, sz, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);

        if (ptr == MAP_FAILED) return support::Result<std::vector<std::uint8_t>, BootError>{std::unexpected{BootError::IoError}};

        std::vector<std::uint8_t> buf(sz);
        std::memcpy(buf.data(), ptr, sz);
        munmap(ptr, sz);

        return support::Result<std::vector<std::uint8_t>, BootError>{std::move(buf)};
    }

    support::Result<VrSymbols, BootError> extract_vr_symbols(std::span<const std::uint8_t> raw_bootimg) {
        // Step 1: Parse header -> kernel segment
        Header hdr{};
        KernelSegment seg{};
        if (!parse_header(raw_bootimg, hdr, seg)) {
            return support::Result<VrSymbols, BootError>{std::unexpected{BootError::HeaderTooShort}};
        }

        // Step 2: Extract kernel segment and validate arm64 Image size
        if (raw_bootimg.size() < seg.offset || raw_bootimg.size() - seg.offset < seg.size) {
            return support::Result<VrSymbols, BootError>{std::unexpected{BootError::ImageTooSmall}};
        }
        auto kernel_ptr = raw_bootimg.data() + seg.offset;
        std::span<const std::uint8_t> kernel_img(kernel_ptr, seg.size);

        auto img_hdr = parse_arm64_image(kernel_img);
        if (!img_hdr.valid || img_hdr.image_size == 0) {
            return support::Result<VrSymbols, BootError>{std::unexpected{BootError::ImageHeaderSizeMismatch}};
        }

        // Step 3: Parse kallsyms
        KallsymsReader ksyms(kernel_img);
        if (!ksyms.valid()) {
            return support::Result<VrSymbols, BootError>{std::unexpected{BootError::BadKallsyms}};
        }

        // Step 4: Find required symbols
        auto sys_exit_tp_opt = ksyms.find_symbol("__tracepoint_sys_exit");
        auto commit_creds_tp_opt = ksyms.find_symbol("__tracepoint_android_rvh_commit_creds");
        auto probestub_opt = ksyms.find_symbol("__probestub_sys_exit");

        if (!sys_exit_tp_opt || !commit_creds_tp_opt || !probestub_opt) {
            return support::Result<VrSymbols, BootError>{std::unexpected{BootError::SymbolNotFound}};
        }

        // Step 5: Get BTF tracepoint funcs offset (fallback 0x48 if unavailable)
        uint64_t funcs_off = 0x48; // default for tracepoint.funcs on PD2463/iQOO15
        {
            BtfReader btf(kernel_img);
            auto funcs_off_opt = btf.tracepoint_funcs_offset();
            if (funcs_off_opt) {
                funcs_off = *funcs_off_opt;
            }
        }
        (void)funcs_off; // profile writer consumes it in recover_sys_exit_tp_funcs

        VrSymbols sym{};
        sym.sys_exit_tp = static_cast<uintptr_t>(*sys_exit_tp_opt);
        sym.commit_creds_tp = static_cast<uintptr_t>(*commit_creds_tp_opt);
        sym.probestub_sys_exit = static_cast<uintptr_t>(*probestub_opt);

        return support::Result<VrSymbols, BootError>{sym};
    }

    support::Result<uint64_t, BootError> recover_sys_exit_tp_funcs(std::span<const std::uint8_t> raw_bootimg) {
        auto sym_res = extract_vr_symbols(raw_bootimg);
        if (!sym_res) {
            return support::Result<uint64_t, BootError>{std::unexpected{sym_res.error()}};
        }

        const auto& sym = sym_res.value();
        uint64_t funcs_off = 0x48;
        {
            BtfReader btf(std::span<const std::uint8_t>(raw_bootimg.data(), raw_bootimg.size()));
            auto opt = btf.tracepoint_funcs_offset();
            if (opt) funcs_off = *opt;
        }

        return support::Result<uint64_t, BootError>{sym.sys_exit_tp + funcs_off};
    }

    support::Result<std::vector<std::uint8_t>, BootError>
    load_boot_image(const char* boot_path, std::size_t max_read) {
        auto res = read_file_to_vec(boot_path);
        if (!res) {
            return support::Result<std::vector<std::uint8_t>, BootError>{std::unexpected{res.error()}};
        }

        auto& buf = *res;
        if (buf.size() > max_read) {
            buf.resize(max_read);
        }

        return support::Result<std::vector<std::uint8_t>, BootError>{std::move(buf)};
    }

} // namespace ghostlock::bootimg
