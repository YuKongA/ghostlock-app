#ifndef GHOSTLOCK_SUPPORT_STATUS_HPP
#define GHOSTLOCK_SUPPORT_STATUS_HPP

namespace ghostlock {
    /* Generic success/failure status for functions whose return carries no data.
     * true == success, false == failure. */
    using Status = bool;
} // namespace ghostlock

#endif
