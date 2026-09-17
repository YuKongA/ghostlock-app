#ifndef GHOSTLOCK_ADDRESS_SPACE_H
#define GHOSTLOCK_ADDRESS_SPACE_H

#include "profile.h"
#ifdef __cplusplus
#include "target_constants.hpp"
#include <optional>
#endif

#include <stdint.h>

typedef enum target_soc_family {
    TARGET_SOC_QCOM = 0,
    TARGET_SOC_MTK,
    TARGET_SOC_XRING,
} TargetSocFamily;

/* Addresses derived once from an immutable target profile and the device SoC.
 * This is the authoritative input for image-to-direct-map translation. */
typedef struct resolved_addresses {
    TargetSocFamily soc;
#ifdef __cplusplus
    ghostlock::target::PhysicalAddress kernel_phys_load;
    ghostlock::target::KernelImageAddress init_cred_image;
#else
    uint64_t kernel_phys_load;
    uintptr_t init_cred_image;
#endif
} ResolvedAddresses;

int resolved_addresses_init(ResolvedAddresses *out,
        const TargetProfile *profile);

int resolved_addresses_init_for_soc(ResolvedAddresses *out,
        const TargetProfile *profile,
        TargetSocFamily soc);

uintptr_t resolved_addresses_data_alias(const ResolvedAddresses *addresses,
        uintptr_t image_addr);

#ifdef __cplusplus
std::optional<ghostlock::target::DirectMapAddress>
resolved_addresses_data_alias_checked(
    const ResolvedAddresses &addresses,
    ghostlock::target::KernelImageAddress image_address) noexcept;

static inline uint64_t resolved_addresses_kernel_phys_load(
    const ResolvedAddresses *addresses) {
  return addresses ? addresses->kernel_phys_load.value() : 0;
}

static inline uintptr_t resolved_addresses_init_cred_image(
    const ResolvedAddresses *addresses) {
  return addresses ? addresses->init_cred_image.value() : 0;
}
#else
static inline uint64_t resolved_addresses_kernel_phys_load(
    const ResolvedAddresses *addresses) {
  return addresses ? addresses->kernel_phys_load : 0;
}

static inline uintptr_t resolved_addresses_init_cred_image(
    const ResolvedAddresses *addresses) {
  return addresses ? addresses->init_cred_image : 0;
}
#endif

const char *resolved_addresses_soc_name(const ResolvedAddresses *addresses,
        const TargetProfile *profile);

#endif
