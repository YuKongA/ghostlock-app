#include "backend/cve_2026_43499/landing/select_stack_route.hpp"
#include "backend/cve_2026_43499/landing/select_stack_route_linux.hpp"

#if defined(__linux__)
namespace ghostlock::backend::cve_2026_43499::landing {
    template class SelectStackRoute<SelectStackRouteLinux>;
}
#endif
