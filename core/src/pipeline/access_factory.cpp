#include "contract/kernel_access.hpp"

/** @file pipeline/access_factory.cpp
 *  The one place that knows which backends this build has.
 *
 *  The contract declares the factory and says where it belongs: defined where the backends are
 *  known, because that layer must not reach for them. This file is that place. Keeping it as a
 *  translation unit rather than a header means the dependency on both backends is confined here,
 *  so the contract stays neutral and nothing else picks up an include it did not ask for.
 *
 *  An id with no implementation returns null rather than a stand-in. The catalogue lists six
 *  backends and four of them are headers only, so a caller that asks for one of those must be told
 *  no -- and then decide, rather than be handed something that fails later with a stranger error.
 */
#include "backend/cve_2026_43284/kernel_access.hpp"
#include "backend/cve_2026_43499/kernel_access.hpp"

namespace ghostlock::contract {
    /* Wiring rule (2026-10-12 ruling). The contract names a backend and nothing
     *  else, so this entry keeps handing back the backend base access.  A caller
     *  that already knows the route asks
     *  cve_2026_43499::create_kernel_access_43499(route) instead, which returns that
     *  route s subclass: right subclass, right parameter type, right wrapper, decided
     *  once at construction.  Switching a route means shutting the first access down
     *  explicitly and creating the second -- never an implicit replacement.
     */
    std::unique_ptr<KernelAccess> KernelAccess::create(BackendId backend) noexcept {
        switch (backend) {
            case BackendId::Cve_2026_43499:
                return std::make_unique<::ghostlock::backend::cve_2026_43499::KernelAccess43499>();
            case BackendId::Cve_2026_43284:
                return std::make_unique<::ghostlock::backend::cve_2026_43284::Access>();
            default:
                /* The catalogue's other four are declarations without implementations. */
                return nullptr;
        }
    }
}  // namespace ghostlock::contract
