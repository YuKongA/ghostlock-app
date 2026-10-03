#include "backend/cve_2026_43499_state.hpp"

#include "memory/target.h"

#include <new>

namespace ghostlock::backend {
    using ghostlock::session::CoreSession;
    Cve2026_43499State::Cve2026_43499State() noexcept {
        addresses.soc = memory::SocFamily::Qcom;
        addresses.kernel_phys_load = target::KernelAddress<target::PhysicalAddressDomain>(
                memory::P0_KERNEL_PHYS_LOAD);
        addresses.init_cred_image = target::KernelAddress<target::ImageAddressDomain>();
        heap.init();
    }

    void cve43499_state_construct(CoreSession &state) noexcept {
        if (state.backend_state_ready) return;
        std::construct_at(reinterpret_cast<Cve2026_43499State *>(state.backend_state));
        state.backend_state_ready = true;
        state.backend_state_dtor = [](void *raw) noexcept {
            static_cast<Cve2026_43499State *>(raw)->~Cve2026_43499State();
        };
    }
} // namespace ghostlock::backend
