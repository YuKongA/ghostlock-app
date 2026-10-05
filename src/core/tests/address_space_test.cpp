/*
 * Host vectors for ResolvedAddresses: the DRAM-base (phys_offset) override and
 * its effect on the image->direct-map translation. No Android headers are
 * needed; init_for_soc takes the SoC family explicitly. The input is the
 * neutral memory::LaunchGeometry POD, so this test also proves the address
 * space no longer needs profile/backend types (ADR-0004 R1).
 */

#include "memory/target.h"
#include "memory/address_space.h"

#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {
    using ghostlock::memory::LaunchGeometry;
    using ghostlock::memory::ResolvedAddresses;
    using ghostlock::memory::SocFamily;

    LaunchGeometry make_geometry(uint64_t init_cred,
                                 std::optional<uint64_t> phys_load,
                                 std::optional<uint64_t> phys_offset) {
        LaunchGeometry geometry{};
        geometry.uname_r = "test";
        geometry.init_cred_offset = init_cred;
        geometry.kernel_phys_load = phys_load;
        geometry.kernel_phys_offset = phys_offset;
        return geometry;
    }
}

int main() {
    using ghostlock::memory::KIMAGE_TEXT_BASE;
    using ghostlock::memory::P0_PAGE_OFFSET;
    using ghostlock::memory::P0_PHYS_OFFSET;

    const uint64_t off = 0x237a0e8; /* e.g. selinux_enforcing */
    const uint64_t image = KIMAGE_TEXT_BASE + off;

    /* MTK6893 (moto): image phys 0x40080000, DRAM base 0x40000000. With the
     * geometry override the translation resolves; without it (default
     * P0_PHYS_OFFSET = 0x80000000) the physical sits below the base and the
     * translation is rejected. */
    {
        const auto geometry = make_geometry(0x1000, 0x40080000ULL, 0x40000000ULL);
        ResolvedAddresses addresses;
        assert(addresses.init_for_soc(geometry, SocFamily::Mtk) == 0);
        assert(addresses.phys_offset == 0x40000000ULL);
        const uintptr_t alias = addresses.data_alias(image);
        assert(alias == (static_cast<uintptr_t>(0x80000ULL + off) | P0_PAGE_OFFSET));
        assert(alias != 0);
    }

    {
        const auto geometry = make_geometry(0x1000, 0x40080000ULL, std::nullopt);
        ResolvedAddresses addresses;
        assert(addresses.init_for_soc(geometry, SocFamily::Mtk) == 0);
        assert(addresses.phys_offset == P0_PHYS_OFFSET);
        assert(addresses.data_alias(image) == 0);
    }

    /* Absent override keeps the compiled default for every existing device. */
    {
        const auto geometry = make_geometry(0x1000, 0xa8000000ULL, std::nullopt);
        ResolvedAddresses addresses;
        assert(addresses.init_for_soc(geometry, SocFamily::Qcom) == 0);
        assert(addresses.phys_offset == P0_PHYS_OFFSET);
    }

    std::puts("address_space_test: ok");
    return 0;
}
