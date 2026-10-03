#ifndef GHOSTLOCK_ADDRESS_SPACE_H
#define GHOSTLOCK_ADDRESS_SPACE_H

#include "profile/model.h"
#include "memory/target_constants.hpp"
#include <optional>

#include <cstdint>

namespace ghostlock::memory {
    enum class SocFamily : int32_t {
        Qcom = 0,
        Mtk,
        Xring,
        Google,
    };

    /* Generic image<->direct-map translation derived once from an immutable
     * target profile and the device SoC. Backend-agnostic. */
    struct AddressSpace {
        SocFamily soc;
        target::KernelAddress<target::PhysicalAddressDomain> kernel_phys_load;
        /* DRAM base (linear-map PHYS_OFFSET). Defaults to the compiled
         * P0_PHYS_OFFSET; a profile may override it via kernel_phys_offset. */
        uintptr_t phys_offset = 0;

        int32_t init_for_soc(const ghostlock::profile::TargetProfile *profile, SocFamily family);

        uintptr_t data_alias(uintptr_t image_addr) const;

        std::optional<target::KernelAddress<target::DirectMapAddressDomain> > data_alias_checked(
            target::KernelAddress<target::ImageAddressDomain> image_address) const noexcept;

        [[nodiscard]] uint64_t phys_load() const {
            return kernel_phys_load.value();
        }

        [[nodiscard]] const char *soc_name(const ghostlock::profile::TargetProfile *profile) const;
    };

    /* cve_2026_43499-resolved addresses: the generic space plus the credential
     * image address. Base-first layout keeps Cve2026_43499State offsets fixed. */
    struct ResolvedAddresses : AddressSpace {
        target::KernelAddress<target::ImageAddressDomain> init_cred_image;

        int32_t init(const ghostlock::profile::TargetProfile *profile);

        [[nodiscard]] uintptr_t init_cred_image_addr() const {
            return init_cred_image.value();
        }
    };
} // namespace ghostlock::memory

#endif
