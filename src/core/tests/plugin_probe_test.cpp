/* S4 P1 plugin probe host tests (contract-design 3.14.7.1 / 3.14.7.2).
 *
 * The probe links the real loader ops (dlopen/dlsym/dlclose) and the shared
 * support/sha256.cpp against real C99 fixtures built by the Makefile, so the TSV
 * contract, the size gate, the hash-before-dlopen rule and the "never runs a
 * hook" guarantee are all exercised end to end. */

#include "plugin/loader.hpp"
#include "plugin/probe.hpp"
#include "support/sha256.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <string_view>
#include <vector>

namespace {
    using ghostlock::plugin::default_loader_ops;
    using ghostlock::plugin::LoaderOps;
    using ghostlock::plugin::probe_plugin;
    using ghostlock::plugin::ProbeSink;

    const char *kPluginDir = GLK_TEST_PLUGIN_DIR;

    struct Capture final {
        std::string text;
    };

    void capture_write(void *ctx, std::string_view line) {
        auto *capture = static_cast<Capture *>(ctx);
        capture->text.append(line.data(), line.size());
        capture->text.push_back('\n');
    }

    std::vector<std::string> lines_of(const Capture &capture) {
        std::vector<std::string> lines;
        std::size_t start = 0u;
        while (start < capture.text.size()) {
            const std::size_t end = capture.text.find('\n', start);
            if (end == std::string::npos) break;
            lines.push_back(capture.text.substr(start, end - start));
            start = end + 1u;
        }
        return lines;
    }

    std::string fixture_path(const char *name) {
        return std::string(kPluginDir) + "/" + name;
    }

    std::string sha256_of(const std::string &path) {
        char hex[ghostlock::support::kSha256HexLength + 1u] = {};
        assert(ghostlock::support::sha256_file(path.c_str(), hex, sizeof(hex)) == 0);
        return std::string(hex);
    }

    int g_open_calls = 0;
    void *counting_open(const char *path) {
        ++g_open_calls;
        return default_loader_ops().open_lib(path);
    }

    int run_probe(const std::string &path, const char *expect, Capture &out,
                  Capture &err, bool count_opens) {
        LoaderOps ops = default_loader_ops();
        if (count_opens) {
            ops.open_lib = &counting_open;
        }
        const ProbeSink out_sink{&capture_write, &out};
        const ProbeSink err_sink{&capture_write, &err};
        return probe_plugin(path, expect, ops, out_sink, err_sink);
    }

    const std::string *find_line(const std::vector<std::string> &lines,
                                 std::string_view prefix) {
        for (const std::string &line : lines) {
            if (line.rfind(prefix, 0u) == 0u) return &line;
        }
        return nullptr;
    }

    std::size_t count_columns(std::string_view line) {
        std::size_t count = 1u;
        for (const char ch : line) {
            if (ch == '\t') ++count;
        }
        return count;
    }
} // namespace

int main() {
    /* Deterministic header root: the probe reads GHOSTLOCK_HOME like the
     * loader does. */
    assert(setenv("GHOSTLOCK_HOME", "/tmp/glk-probe-home", 1) == 0);

    /* ---- 1. Frozen header + plugin/hook rows on the v2 schema fixture. ---- */
    {
        const std::string path = fixture_path("cm_test_plugin_schema.so");
        Capture out;
        Capture err;
        assert(run_probe(path, nullptr, out, err, false) == 0);
        const std::vector<std::string> lines = lines_of(out);
        assert(lines.size() >= 10u);
        assert(lines[0] == "host_abi\t1");
        /* The relative name only: the App compares this literal, never a path. */
        assert(lines[1] == "countermeasures_root\tcountermeasures");
        assert(lines[2] ==
               "host_stages\tpre_spawn,post_spawn,pre_terminal,post_terminal");
        assert(lines[3] == "host_caps\tkernel_read,kernel_write,alias,child_task");
        /* P1 revision: the backend stage matrix travels in the header so the App
         * never hard-codes it (single authority: plugin/schema.hpp). */
        assert(lines[4] ==
               "stage_availability\t43499:pre_terminal;43284:post_terminal");
        const std::string *plugin = find_line(lines, "plugin\t");
        assert(plugin != nullptr);
        assert(count_columns(*plugin) == 8u);
        assert(*plugin == "plugin\ttest.schema\t1.2.3\t1\t80\t" + sha256_of(path) +
                          "\tpre_spawn,post_terminal\tkernel_read,alias");
        const std::string *hook = find_line(lines, "hook\t");
        assert(hook != nullptr);
        assert(count_columns(*hook) == 6u);
        assert(*hook == "hook\ttest.schema\ton_stage\tpost_terminal\t10\tschema-hook");
        /* param rows: frozen column order, type literals, defaults. */
        assert(find_line(lines, "param\ttest.schema\tthreshold\tuint\t1\t200\tuint parameter") != nullptr);
        assert(find_line(lines, "param\ttest.schema\tmode\tstr\t0\tauto\tstring parameter") != nullptr);
        assert(find_line(lines, "param\ttest.schema\tenabled\tbool\t0\t1\tbool parameter") != nullptr);
        /* Audit D1: an INT default is signed; -5 must not print as 2^64-5. */
        assert(find_line(lines, "param\ttest.schema\tdelta\tint\t0\t-5\t-") != nullptr);
        assert(find_line(lines, "extract\ttest.schema\ttask_offset\tuint\t1\t0\textractor-provided offset") != nullptr);
        assert(find_line(lines, "reject\t") == nullptr);
        assert(err.text.empty());
    }

    /* ---- 2. The probe never runs a hook. ---- */
    {
        const std::string path = fixture_path("cm_test_plugin_schema.so");
        Capture out;
        Capture err;
        assert(run_probe(path, nullptr, out, err, false) == 0);
        void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        assert(handle != nullptr);
        auto *calls = reinterpret_cast<int (*)()>(dlsym(handle, "glk_probe_test_hook_calls"));
        assert(calls != nullptr);
        assert(calls() == 0);
        (void)dlclose(handle);
    }

    /* ---- 3. --expect-sha256: mismatch rejects BEFORE dlopen. ---- */
    {
        const std::string path = fixture_path("cm_test_plugin_schema.so");
        Capture out;
        Capture err;
        g_open_calls = 0;
        const char *zero_hash =
                "0000000000000000000000000000000000000000000000000000000000000000";
        assert(run_probe(path, zero_hash, out, err, true) == 1);
        assert(g_open_calls == 0);
        assert(out.text == "reject\t-\tHashMismatch\n");
        assert(err.text.find("HashMismatch") != std::string::npos);
    }
    {
        /* The matching hash passes and the description is produced. */
        const std::string path = fixture_path("cm_test_plugin_schema.so");
        Capture out;
        Capture err;
        const std::string digest = sha256_of(path);
        assert(run_probe(path, digest.c_str(), out, err, false) == 0);
        const std::vector<std::string> lines = lines_of(out);
        assert(find_line(lines, "plugin\t") != nullptr);
    }
    {
        /* A malformed hash is an argument error, not a silent accept. */
        Capture out;
        Capture err;
        g_open_calls = 0;
        assert(run_probe(fixture_path("cm_test_plugin_schema.so"), "zz", out, err,
                         true) == 1);
        assert(g_open_calls == 0);
        assert(out.text == "reject\t-\tInvalidArgument\n");
    }

    /* ---- 4. Hard failures keep the loader vocabulary. ---- */
    {
        Capture out;
        Capture err;
        assert(run_probe(fixture_path("cm_test_plugin_badabi.so"), nullptr, out, err,
                         false) == 1);
        assert(out.text == "reject\tghostlock.badabi\tAbiMismatch\n");
    }
    {
        Capture out;
        Capture err;
        assert(run_probe(fixture_path("cm_test_plugin_missing_entry.so"), nullptr, out,
                         err, false) == 1);
        assert(out.text == "reject\t-\tEntryMissing\n");
    }
    {
        Capture out;
        Capture err;
        assert(run_probe(fixture_path("does-not-exist.so"), nullptr, out, err, false) == 1);
        assert(out.text == "reject\t-\tHashRejected\n");
    }

    /* ---- 5. v1-size module: the poisoned tail is never read. ---- */
    {
        const std::string path = fixture_path("cm_test_plugin_v1size.so");
        Capture out;
        Capture err;
        assert(run_probe(path, nullptr, out, err, false) == 0);
        const std::vector<std::string> lines = lines_of(out);
        const std::string *plugin = find_line(lines, "plugin\t");
        assert(plugin != nullptr);
        assert(*plugin == "plugin\ttest.v1size\t0.9.0\t1\t40\t" + sha256_of(path) +
                          "\tpre_spawn\t-");
        assert(find_line(lines, "param\t") == nullptr);
        assert(find_line(lines, "extract\t") == nullptr);
        assert(find_line(lines, "reject\t") == nullptr);
    }

    /* ---- 5b. Audit D4/D7: a hook with a NULL name and a parameter with a
     * control byte are NOT published as rows; only reject rows name them. ---- */
    {
        const std::string path = fixture_path("cm_test_plugin_nullhook.so");
        Capture out;
        Capture err;
        assert(run_probe(path, nullptr, out, err, false) == 0);
        const std::vector<std::string> lines = lines_of(out);
        assert(find_line(lines, "plugin\tdemo.nullhook\t") != nullptr);
        assert(find_line(lines, "hook\t") == nullptr);
        assert(find_line(lines, "param\t") == nullptr);
        assert(find_line(lines, "reject\tdemo.nullhook\tHooksMissing") != nullptr);
        assert(find_line(lines, "reject\tdemo.nullhook\tInvalidArgument") != nullptr);
    }

    /* ---- 6. The good v1-style fixture still describes (tail zeroed). ---- */
    {
        const std::string path = fixture_path("cm_test_plugin.so");
        Capture out;
        Capture err;
        assert(run_probe(path, nullptr, out, err, false) == 0);
        const std::vector<std::string> lines = lines_of(out);
        const std::string *plugin = find_line(lines, "plugin\t");
        assert(plugin != nullptr);
        assert(plugin->rfind("plugin\tghostlock.test\t1.0.0\t1\t", 0u) == 0u);
    }

    puts("plugin_probe_test: ok");
    return 0;
}
