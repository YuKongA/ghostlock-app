#ifndef GHOSTLOCK_SUPPORT_FATAL_ERROR_HPP
#define GHOSTLOCK_SUPPORT_FATAL_ERROR_HPP

namespace ghostlock {
    /* Thrown only on pre-attack fatal errors where execution must stop. The
     * throwing site logs the details; the top-level handler just exits.
     * Never thrown from the PI-race / fork / heap-spray timing windows. */
    struct FatalError final {};
} // namespace ghostlock

#endif
