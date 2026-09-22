#include "memory/address_space.h"

#include "kernel/target.h"

#include <cerrno>
#include <array>
#include <cstring>
#include <string_view>
#include <strings.h>

/* Measured direct-map end. Defaults to the built-in bound and can only be
 * narrowed by a rooted /proc/iomem dump (apply_iomem_cache). KernelSnitch
 * reads it to bound its scan; it is never an authority wider than target.h. */
namespace ghostlock::kernel {
    uint64_t g_direct_map_end = DIRECT_MAP_END;
} // namespace ghostlock::kernel

namespace ghostlock::memory {
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include <limits>

    /* Android property lookup is unavailable in host fixed-vector tests. The
 * non-Android branch keeps the QCOM default so the deterministic SoC-specific
 * derivations below can be exercised on the host; production is unchanged. */
#if defined(__ANDROID__)
    static SocFamily detect_target_soc(void) {
        std::array < char, 256 > value{};
        const char *keys[] = {
            "ro.soc.manufacturer", "ro.soc.model",
            "ro.board.platform", nullptr
        };
        for (int32_t i = 0; keys[i]; ++i) {
            if (__system_property_get(keys[i], value.data()) <= 0 || !value[0]) continue;
            if (strncasecmp(value.data(), "google", 6) == 0 ||
                strncasecmp(value.data(), "tensor", 6) == 0 ||
                (i > 0 && (strncasecmp(value.data(), "gs", 2) == 0 ||
                           strncasecmp(value.data(), "zuma", 4) == 0))) {
                return SocFamily::Google;
            }
        }
        for (int32_t i = 0; keys[i]; ++i) {
            if (__system_property_get(keys[i], value.data()) <= 0 || !value[0]) continue;
            if (strncasecmp(value.data(), "mediatek", 8) == 0 ||
                strncasecmp(value.data(), "mtk", 3) == 0 ||
                (i > 0 && strncasecmp(value.data(), "mt", 2) == 0)) {
                return SocFamily::Mtk;
            }
        }
        for (int32_t i = 0; keys[i]; ++i) {
            if (__system_property_get(keys[i], value.data()) <= 0 || !value[0]) continue;
            if (strncasecmp(value.data(), "xring", 5) == 0 ||
                (i > 0 && strncasecmp(value.data(), "o1", 2) == 0)) {
                return SocFamily::Xring;
            }
        }
        return SocFamily::Qcom;
    }
#else
    static SocFamily detect_target_soc(void) {
        return SocFamily::Qcom;
    }
#endif

    int32_t ResolvedAddresses::init_for_soc(const profile::TargetProfile *profile,
                                        SocFamily family) {
        const profile::kernel_offsets *values = profile->values();
        if (!values || !values->uname_r || !values->off_init_cred) {
            errno = EINVAL;
            return -1;
        }
        *this = ResolvedAddresses{};
        soc = family;
        const auto image = target::KernelAddress<target::ImageAddressDomain>(kernel::KIMAGE_TEXT_BASE)
                .checked_add(values->off_init_cred);
        if (!image) {
            errno = ERANGE;
            return -1;
        }
        init_cred_image = *image;
        if (values->kernel_phys_load) {
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                values->kernel_phys_load);
        } else if (soc == SocFamily::Mtk || soc == SocFamily::Google) {
            /* Tensor G4/G5 (zumapro) loads the Image at the DRAM base like MTK. */
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                kernel::KIMAGE_TEXT_BASE - kernel::MTK_VADDR_BASE);
        } else if (soc == SocFamily::Xring) {
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                kernel::XRING_KERNEL_PHYS_LOAD);
        } else if (std::string_view(values->uname_r).starts_with("6.12.")) {
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                kernel::QC_GKI_6_12_PHYS_LOAD);
        } else {
            kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                kernel::P0_KERNEL_PHYS_LOAD);
        }
        return 0;
    }

    int32_t ResolvedAddresses::init(const profile::TargetProfile *profile) {
        return init_for_soc(profile, detect_target_soc());
    }

    uintptr_t ResolvedAddresses::data_alias(uintptr_t image_addr) const {
        const auto result = data_alias_checked(
            target::KernelAddress<target::ImageAddressDomain>(image_addr));
        return result ? result->value() : 0;
    }

    std::optional<target::KernelAddress<target::DirectMapAddressDomain> >
    ResolvedAddresses::data_alias_checked(
        target::KernelAddress<target::ImageAddressDomain> image_address) const noexcept {
        const uintptr_t image = image_address.value();
        if (image < kernel::KIMAGE_TEXT_BASE) return std::nullopt;
        const uintptr_t offset = image - kernel::KIMAGE_TEXT_BASE;
        const auto physical = kernel_phys_load.checked_add(offset);
        if (!physical || physical->value() < kernel::P0_PHYS_OFFSET) return std::nullopt;
        const uintptr_t direct =
                (physical->value() - kernel::P0_PHYS_OFFSET) | kernel::P0_PAGE_OFFSET;
        if (direct < kernel::P0_PAGE_OFFSET) return std::nullopt;
        return target::KernelAddress<target::DirectMapAddressDomain>(direct);
    }

    const char *ResolvedAddresses::soc_name(const profile::TargetProfile *profile) const {
        if (soc == SocFamily::Mtk) return "mtk";
        if (soc == SocFamily::Xring) return "xring";
        const profile::kernel_offsets *values = profile->values();
        if (soc == SocFamily::Google) {
            return values && values->kernel_phys_load ? "google/tensor" : "tensor";
        }
        return values && !values->kernel_phys_load && values->uname_r &&
               std::string_view(values->uname_r).starts_with("6.12.")
                   ? "qcom/6.12"
                   : "qcom/other";
    }
} // namespace ghostlock::memory
