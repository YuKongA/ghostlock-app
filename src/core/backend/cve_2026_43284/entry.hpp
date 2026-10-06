#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_ENTRY_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_ENTRY_HPP

/* ADR-0006 T4: the cve_2026_43284 entry seam main.cpp names.
 *
 * main.cpp is the composition root, not a backend consumer: it must not include
 * the backend's concrete headers directly (diagnostic.hpp, execution_binding.hpp,
 * session_frame.hpp, stage_runner.hpp). The read-only diagnostic entry and the
 * side-channel read are exposed here; the concrete headers are included
 * transitively by this seam. Behavior is unchanged -- each function forwards to
 * the shipped implementation, and ProductionSession holds the same resources by
 * value on the stack, so no heap allocation is introduced on any path (including
 * 43499). The dev/gate staged entry (--run-cve-2026-43284) was removed in S4 R2b. */

#include "backend/cve_2026_43284/execution_binding.hpp"
#include "backend/cve_2026_43284/session_frame.hpp"
#include "profile/document.hpp"

#include <cstdint>
#include <string_view>

namespace ghostlock::session {
    struct CoreSession;
}

namespace ghostlock::backend::cve_2026_43284::entry {

    /* Read-only diagnostic entry (--probe-cve-2026-43284 <ko>). Returns the
     * diagnostic process exit code. */
    [[nodiscard]] int run_diagnostic(std::string_view module_path);

    class ProductionSession final {
    public:
        ProductionSession() = default;
        ~ProductionSession() = default;

        ProductionSession(const ProductionSession &) = delete;
        ProductionSession &operator=(const ProductionSession &) = delete;
        ProductionSession(ProductionSession &&) = delete;
        ProductionSession &operator=(ProductionSession &&) = delete;

        /* Read and validate the optional session-secret frame from fd. Returns
         * true on Ok, false on any rejection (the caller fails closed). */
        [[nodiscard]] bool read_side_channel(int fd) noexcept;

        /* Construct the backend state and bind the production resources. Returns
         * the ExecutionBindError value (0 == None). allow_dev_target is the
         * --allow-dev-target safety switch: it lets the single carrier be a
         * non-vendor one-shot path (device gates only) and is forwarded to the
         * carrier selection and the chain's carrier validation. plugin_host is
         * the borrowed S4 P1 host (step 3a): bind stores it in the chain context
         * so the LKM window can dispatch POST_TERMINAL; null keeps the window
         * plugin-free. The caller keeps it alive across the pipeline call. */
        [[nodiscard]] std::uint8_t bind(session::CoreSession &session,
                                        const profile::Document &document,
                                        bool allow_dev_target,
                                        plugin::PluginHost *plugin_host = nullptr);

    private:
        ScopedIpsecSaParams secrets_{};
        ProductionResources production_{};
    };

} // namespace ghostlock::backend::cve_2026_43284::entry

#endif
