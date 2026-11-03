#include "host_attack_script.hpp"

namespace ghostlock::host {
    HostAttackScript &script() noexcept {
        static HostAttackScript instance;
        return instance;
    }
} // namespace ghostlock::host
