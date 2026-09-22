#include "memory/address_space.h"

#include "target.h"

#include <cerrno>
#include <cstring>
#include <strings.h>

/* Measured direct-map end. Defaults to the built-in bound and can only be
 * narrowed by a rooted /proc/iomem dump (apply_iomem_cache). KernelSnitch
 * reads it to bound its scan; it is never an authority wider than target.h. */
uint64_t g_direct_map_end = DIRECT_MAP_END;

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
        char value[256];
        const char *keys[] = {
            "ro.soc.manufacturer", "ro.soc.model",
            "ro.board.platform", nullptr
        };
        for (int i = 0; keys[i]; ++i) {
            if (__system_property_get(keys[i], value) <= 0 || !value[0]) continue;
            if (strncasecmp(value, "google", 6) == 0 ||
                strncasecmp(value, "tensor", 6) == 0 ||
                (i > 0 && (strncasecmp(value, "gs", 2) == 0 ||
                           strncasecmp(value, "zuma", 4) == 0))) {
                return SocFamily::Google;
            }
        }
        for (int i = 0; keys[i]; ++i) {
            if (__system_property_get(keys[i], value) <= 0 || !value[0]) continue;
            if (strncasecmp(value, "mediatek", 8) == 0 ||
                strncasecmp(value, "mtk", 3) == 0 ||
                (i > 0 && strncasecmp(value, "mt", 2) == 0)) {
                return SocFamily::Mtk;
            }
        }
        for (int i = 0; keys[i]; ++i) {
            if (__system_property_get(keys[i], value) <= 0 || !value[0]) continue;
            if (strncasecmp(value, "xring", 5) == 0 ||
                (i > 0 && strncasecmp(value, "o1", 2) == 0)) {
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

    int ResolvedAddresses::init_for_soc(const TargetProfile *profile,
                                        SocFamily family) {
        const struct kernel_offsets *values = profile->values();
        if (!values || !values->uname_r || !values->off_init_cred) {
            errno = EINVAL;
            return -1;
        }
        *this = ResolvedAddresses{};
        soc = family;
        const auto image = target::KernelImageAddress(KIMAGE_TEXT_BASE)
                .checked_add(values->off_init_cred);
        if (!image) {
            errno = ERANGE;
            return -1;
        }
        init_cred_image = *image;
        if (values->kernel_phys_load) {
            kernel_phys_load = target::PhysicalAddress(
                values->kernel_phys_load);
        } else if (soc == SocFamily::Mtk || soc == SocFamily::Google) {
            /* Tensor G4/G5 (zumapro) loads the Image at the DRAM base like MTK. */
            kernel_phys_load = target::PhysicalAddress(
                KIMAGE_TEXT_BASE - MTK_VADDR_BASE);
        } else if (soc == SocFamily::Xring) {
            kernel_phys_load = target::PhysicalAddress(
                XRING_KERNEL_PHYS_LOAD);
        } else if (strncmp(values->uname_r, "6.12.", 5) == 0) {
            kernel_phys_load = target::PhysicalAddress(
                QC_GKI_6_12_PHYS_LOAD);
        } else {
            kernel_phys_load = target::PhysicalAddress(
                P0_KERNEL_PHYS_LOAD);
        }
        return 0;
    }

    int ResolvedAddresses::init(const TargetProfile *profile) {
        return init_for_soc(profile, detect_target_soc());
    }

    uintptr_t ResolvedAddresses::data_alias(uintptr_t image_addr) const {
        const auto result = data_alias_checked(
            target::KernelImageAddress(image_addr));
        return result ? result->value() : 0;
    }

    std::optional<target::DirectMapAddress>
    ResolvedAddresses::data_alias_checked(
        target::KernelImageAddress image_address) const noexcept {
        const uintptr_t image = image_address.value();
        if (image < KIMAGE_TEXT_BASE) return std::nullopt;
        const uintptr_t offset = image - KIMAGE_TEXT_BASE;
        const auto physical = kernel_phys_load.checked_add(offset);
        if (!physical || physical->value() < P0_PHYS_OFFSET) return std::nullopt;
        const uintptr_t direct =
                (physical->value() - P0_PHYS_OFFSET) | P0_PAGE_OFFSET;
        if (direct < P0_PAGE_OFFSET) return std::nullopt;
        return target::DirectMapAddress(direct);
    }

    const char *ResolvedAddresses::soc_name(const TargetProfile *profile) const {
        if (soc == SocFamily::Mtk) return "mtk";
        if (soc == SocFamily::Xring) return "xring";
        const struct kernel_offsets *values = profile->values();
        if (soc == SocFamily::Google) {
            return values && values->kernel_phys_load ? "google/tensor" : "tensor";
        }
        return values && !values->kernel_phys_load && values->uname_r &&
               strncmp(values->uname_r, "6.12.", 5) == 0
                   ? "qcom/6.12"
                   : "qcom/other";
    }
} // namespace ghostlock::memory
