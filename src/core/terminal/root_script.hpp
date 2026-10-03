#ifndef GHOSTLOCK_TERMINAL_ROOT_SCRIPT_HPP
#define GHOSTLOCK_TERMINAL_ROOT_SCRIPT_HPP

namespace ghostlock::terminal {
    /* Write the root-side KernelSU handoff script to the configured path. */
    void write_root_script(void);
} // namespace ghostlock::terminal

#endif
