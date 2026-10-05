#ifndef GHOSTLOCK_TERMINAL_ROOT_PROGRAM_HPP
#define GHOSTLOCK_TERMINAL_ROOT_PROGRAM_HPP

/* Compatibility header: the neutral RootProgram spec is identity/interface
 * vocabulary and now lives in contract/identity.hpp (ADR-0004: contract carries
 * the neutral terminal interface so the execution concepts don't create a
 * contract -> terminal include edge). It is re-exported here under
 * ghostlock::terminal to keep the existing terminal-facing call sites. */

#include "contract/identity.hpp"

namespace ghostlock::terminal {
    using contract::RootProgram;
    using contract::RootProgramKind;
} // namespace ghostlock::terminal

#endif
