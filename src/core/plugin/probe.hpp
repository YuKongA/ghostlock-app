#ifndef GHOSTLOCK_PLUGIN_PROBE_HPP
#define GHOSTLOCK_PLUGIN_PROBE_HPP

/* S4 P1 read-only plugin probe (contract-design 3.14.7.1 / 3.14.7.2).
 *
 * --plugin-probe <path.so> [--expect-sha256 <hex>] loads ONE out-of-tree
 * countermeasure in a dedicated process, reads its static self-description and
 * exits. It never registers a module, never installs a hook, never runs a hook
 * and never keeps the library mapped past the description read: the handle is
 * closed before returning.
 *
 * stdout carries ONLY the frozen TSV description (kind-first rows, header keys
 * before them); diagnostics go to stderr. Exit code 0 means a description was
 * produced (a reject row names a registration-time rejection, which the App
 * greys out); non-zero means the probe itself failed (path / hash / open /
 * entry / ABI / size), in which case only a reject row is emitted.
 *
 * --expect-sha256 reuses support::sha256_file (the same implementation the
 * loader hashes with) and is checked BEFORE dlopen: a mismatch never loads the
 * library. */

#include "plugin/loader.hpp"

#include <cstdint>
#include <string_view>

namespace ghostlock::plugin {
    /* One output line (without the trailing newline). */
    struct ProbeSink final {
        void (*write)(void *ctx, std::string_view line) = nullptr;
        void *ctx = nullptr;
    };

    /* Testable core: produces the TSV on out, diagnostics on err. Returns 0
     * when the description was produced, 1 on a probe failure. */
    [[nodiscard]] int probe_plugin(std::string_view path, const char *expect_sha256,
                                   const LoaderOps &ops, const ProbeSink &out,
                                   const ProbeSink &err) noexcept;

    /* Process entry for --plugin-probe: default loader ops, stdout/stderr
     * sinks and PR_SET_NO_NEW_PRIVS before the library is opened (Linux).
     * Returns the process exit code. */
    [[nodiscard]] int run_plugin_probe(std::string_view path,
                                       const char *expect_sha256) noexcept;
} // namespace ghostlock::plugin

#endif
