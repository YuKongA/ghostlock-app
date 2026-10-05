#ifndef GHOSTLOCK_TERMINAL_ROOT_SCRIPT_HPP
#define GHOSTLOCK_TERMINAL_ROOT_SCRIPT_HPP

namespace ghostlock::terminal {
    /* Write the root-side KernelSU handoff script to the configured path.
     * safe_mode comes from the caller (ADR-0006 T1): the text generator must not
     * read backend state, so the handoff module owns that read. */
    void write_root_script(bool safe_mode);
} // namespace ghostlock::terminal

#endif
