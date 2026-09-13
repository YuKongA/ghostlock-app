#ifndef GHOSTLOCK_ADDRESS_SPACE_H
#define GHOSTLOCK_ADDRESS_SPACE_H

#include "profile.h"

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
  uint64_t kernel_phys_load;
  uintptr_t init_cred_image;
} ResolvedAddresses;

int resolved_addresses_init(ResolvedAddresses *out,
                            const TargetProfile *profile);
int resolved_addresses_init_for_soc(ResolvedAddresses *out,
                                    const TargetProfile *profile,
                                    TargetSocFamily soc);
uintptr_t resolved_addresses_data_alias(const ResolvedAddresses *addresses,
                                        uintptr_t image_addr);
const char *resolved_addresses_soc_name(const ResolvedAddresses *addresses,
                                        const TargetProfile *profile);

#endif
