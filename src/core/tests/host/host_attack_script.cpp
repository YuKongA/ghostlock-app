#include "host_attack_script.hpp"

namespace lsprivilege::host {
    HostAttackScript &script() noexcept {
        static HostAttackScript instance;
        return instance;
    }
} // namespace lsprivilege::host
