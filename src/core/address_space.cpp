#include "address_space.h"

#include "target.h"

#include <errno.h>
#include <string.h>
#include <strings.h>
#include <sys/system_properties.h>

static TargetSocFamily detect_target_soc(void) {
  char value[256];
  const char *keys[] = {"ro.soc.manufacturer", "ro.soc.model",
                        "ro.board.platform", NULL};
  for (int i = 0; keys[i]; ++i) {
    if (__system_property_get(keys[i], value) <= 0 || !value[0]) continue;
    if (strncasecmp(value, "mediatek", 8) == 0 ||
        strncasecmp(value, "mtk", 3) == 0 ||
        (i > 0 && strncasecmp(value, "mt", 2) == 0)) {
      return TARGET_SOC_MTK;
    }
  }
  for (int i = 0; keys[i]; ++i) {
    if (__system_property_get(keys[i], value) <= 0 || !value[0]) continue;
    if (strncasecmp(value, "xring", 5) == 0 ||
        (i > 0 && strncasecmp(value, "o1", 2) == 0)) {
      return TARGET_SOC_XRING;
    }
  }
  return TARGET_SOC_QCOM;
}

int resolved_addresses_init_for_soc(ResolvedAddresses *out,
                                    const TargetProfile *profile,
                                    TargetSocFamily soc) {
  const struct kernel_offsets *values = target_profile_values(profile);
  if (!out || !values || !values->uname_r || !values->off_init_cred) {
    errno = EINVAL;
    return -1;
  }
  memset(out, 0, sizeof(*out));
  out->soc = soc;
  out->init_cred_image = KIMAGE_TEXT_BASE + values->off_init_cred;
  if (values->kernel_phys_load) {
    out->kernel_phys_load = values->kernel_phys_load;
  } else if (out->soc == TARGET_SOC_MTK) {
    out->kernel_phys_load = KIMAGE_TEXT_BASE - MTK_VADDR_BASE;
  } else if (out->soc == TARGET_SOC_XRING) {
    out->kernel_phys_load = XRING_KERNEL_PHYS_LOAD;
  } else if (strncmp(values->uname_r, "6.12.", 5) == 0) {
    out->kernel_phys_load = QC_GKI_6_12_PHYS_LOAD;
  } else {
    out->kernel_phys_load = P0_KERNEL_PHYS_LOAD;
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
  uintptr_t offset = image_addr - KIMAGE_TEXT_BASE;
  uintptr_t physical = addresses->kernel_phys_load + offset;
  return ((physical - P0_PHYS_OFFSET) | P0_PAGE_OFFSET);
}

const char *resolved_addresses_soc_name(const ResolvedAddresses *addresses,
                                        const TargetProfile *profile) {
  if (!addresses) return "unknown";
  if (addresses->soc == TARGET_SOC_MTK) return "mtk";
  if (addresses->soc == TARGET_SOC_XRING) return "xring";
  const struct kernel_offsets *values = target_profile_values(profile);
  return values && !values->kernel_phys_load && values->uname_r &&
                 strncmp(values->uname_r, "6.12.", 5) == 0
             ? "qcom/6.12"
             : "qcom/other";
}
