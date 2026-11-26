#ifndef LSPRIVILEGE_SUPPORT_STATUS_HPP
#define LSPRIVILEGE_SUPPORT_STATUS_HPP

namespace lsprivilege {
    /* Generic success/failure status for functions whose return carries no data.
     * true == success, false == failure. */
    using Status = bool;
} // namespace lsprivilege

#endif
