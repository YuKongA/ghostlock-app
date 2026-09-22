#ifndef GHOSTLOCK_ADDRESS_SPACE_H
#define GHOSTLOCK_ADDRESS_SPACE_H

#include "profile.h"
#include "target_constants.hpp"
#include <optional>

#include <cstdint>

namespace ghostlock::memory {
    enum class SocFamily : int {
        Qcom = 0,
        Mtk,
        Xring,
        Google,
    };

    /* Addresses derived once from an immutable target profile and the device SoC.
 * This is the authoritative input for image-to-direct-map translation. */
    struct ResolvedAddresses {
        SocFamily soc;
        target::PhysicalAddress kernel_phys_load;
        target::KernelImageAddress init_cred_image;

        int init(const ghostlock::profile::TargetProfile *profile);
        int init_for_soc(const ghostlock::profile::TargetProfile *profile, SocFamily family);

        uintptr_t data_alias(uintptr_t image_addr) const;
        std::optional<target::DirectMapAddress> data_alias_checked(
            target::KernelImageAddress image_address) const noexcept;

        [[nodiscard]] uint64_t phys_load() const {
            return kernel_phys_load.value();
        }
        [[nodiscard]] uintptr_t init_cred_image_addr() const {
            return init_cred_image.value();
        }
        [[nodiscard]] const char *soc_name(const ghostlock::profile::TargetProfile *profile) const;
    };
} // namespace ghostlock::memory

#endif
