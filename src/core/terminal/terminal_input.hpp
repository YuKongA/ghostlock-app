#ifndef GHOSTLOCK_TERMINAL_TERMINAL_INPUT_HPP
#define GHOSTLOCK_TERMINAL_TERMINAL_INPUT_HPP

#include "terminal/root_program.hpp"

namespace ghostlock::terminal {
    /* How a terminal launches the root program (ADR-0004 R19/R20). Descendant
     * inherits the entry process's seccomp filter; KernelSpawned (UMH) does not. */
    enum class ActivationContext : std::uint8_t { Descendant, KernelSpawned };

    /* Neutral terminal input (ADR-0004 R10/D2): what a backend hands to the
     * terminal, independent of the concrete terminal. Every terminal input
     * carries the App-selected root program. RootedChild and UmhForwardInput
     * derive from it; the pipeline passes the base reference. */
    struct TerminalInput {
        RootProgram root_program{};
    };

    /* Input for the umh_forward terminal: the backend reports the UMH/LKM state
     * and the terminal forwards to the selected root program. */
    struct UmhForwardInput : TerminalInput {
        bool lkm_loaded = false;
    };
} // namespace ghostlock::terminal

#endif
