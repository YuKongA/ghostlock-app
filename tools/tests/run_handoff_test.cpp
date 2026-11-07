/**
 * @file tools/tests/run_handoff_test.cpp
 * @brief Exercise the production fixed-path common handoff helper on a POSIX host.
 */

#include "common/gl_log.h"
#include "common/run_handoff.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/types.h>
#include <unistd.h>

namespace {
    const std::string HEADER = "#!/system/bin/sh\n# GHOSTLOCK_HANDOFF_V1\n";

    /** @return A unique test-owned directory with the production relative layout. */
    std::filesystem::path create_home() {
        std::string pattern = (std::filesystem::temp_directory_path() / "ghostlock-handoff-XXXXXX").string();
        std::array<char, 256U> buffer{};
        assert(pattern.size() + 1U <= buffer.size());
        pattern.copy(buffer.data(), pattern.size());
        buffer[pattern.size()] = '\0';
        char *const created = mkdtemp(buffer.data());
        assert(created != nullptr);
        const std::filesystem::path home(created);
        assert(std::filesystem::create_directory(home / "root-script"));
        return home;
    }

    /** Write one complete script at the production fixed relative path. */
    void write_script(const std::filesystem::path &home, const std::string &body) {
        std::ofstream output(home / "root-script" / "root-script.sh", std::ios::binary | std::ios::trunc);
        assert(output.is_open());
        output << body;
        output.close();
        assert(output.good());
    }

    /** Execute with one home and capture the child stdout/stderr routed through the borrowed logger fd. */
    std::pair<std::int32_t, std::string> execute(const std::filesystem::path &home) {
        assert(setenv("GHOSTLOCK_HOME", home.c_str(), 1) == 0);
        std::array<int, 2U> pipe_fds{-1, -1};
        assert(pipe(pipe_fds.data()) == 0);
        assert(gl_log_attach_fd(pipe_fds[1], 0, GL_LOG_INFO) >= 0);
        const std::int32_t status = run_handoff();
        assert(gl_log_attach_fd(-1, 0, GL_LOG_INFO) == -1);
        assert(close(pipe_fds[1]) == 0);
        std::array<char, 1024U> bytes{};
        const ssize_t count = read(pipe_fds[0], bytes.data(), bytes.size());
        assert(count >= 0);
        assert(close(pipe_fds[0]) == 0);
        return {
            status,
            std::string(bytes.data(), static_cast<std::size_t>(count)),
        };
    }

    /** Verify fixed location, type, no-follow, and exact magic failures. */
    void test_locator_and_magic() {
        assert(unsetenv("GHOSTLOCK_HOME") == 0);
        assert(run_handoff() == GL_HANDOFF_HOME_MISSING);
        assert(setenv("GHOSTLOCK_HOME", "relative", 1) == 0);
        assert(run_handoff() == GL_HANDOFF_HOME_INVALID);

        const std::filesystem::path absent = std::filesystem::temp_directory_path() / "ghostlock-absent-home";
        assert(setenv("GHOSTLOCK_HOME", absent.c_str(), 1) == 0);
        assert(run_handoff() == GL_HANDOFF_OPEN_HOME);

        const std::filesystem::path home = create_home();
        assert(setenv("GHOSTLOCK_HOME", home.c_str(), 1) == 0);
        assert(run_handoff() == GL_HANDOFF_OPEN_SCRIPT);
        write_script(home, "#!/system/bin/sh\n# wrong\nexit 0\n");
        assert(run_handoff() == GL_HANDOFF_SCRIPT_MAGIC);
        assert(std::filesystem::remove(home / "root-script" / "root-script.sh"));
        assert(std::filesystem::create_directory(home / "root-script" / "root-script.sh"));
        assert(run_handoff() == GL_HANDOFF_SCRIPT_TYPE);
        std::filesystem::remove_all(home);

        const std::filesystem::path linked_home = create_home();
        const std::filesystem::path link = linked_home.parent_path() / (linked_home.filename().string() + "-link");
        std::filesystem::create_directory_symlink(linked_home, link);
        assert(std::filesystem::is_symlink(link));
        assert(setenv("GHOSTLOCK_HOME", link.c_str(), 1) == 0);
        assert(run_handoff() == GL_HANDOFF_OPEN_HOME);
        assert(std::filesystem::remove(link));
        std::filesystem::remove_all(linked_home);
    }

    /** Verify success output, nonzero exits, exit 127 ambiguity, and signal mapping. */
    void test_child_results_and_log_route() {
        const std::filesystem::path home = create_home();
        write_script(
            home,
            HEADER +
                "false\n"
                "DIAGNOSE_STATUS=$?\n"
                "if [ \"$DIAGNOSE_STATUS\" -ne 1 ]; then exit 126; fi\n"
                "printf '%s\\n' '[I/0ms] diagnose:shell:handoff-ok'\n"
                "exit 0\n"
        );
        const std::pair<std::int32_t, std::string> success = execute(home);
        assert(success.first == 0);
        assert(success.second == "[I/0ms] diagnose:shell:handoff-ok\n");

        write_script(home, HEADER + "exit 42\n");
        assert(execute(home).first == 42);
        write_script(home, HEADER + "exit 127\n");
        assert(execute(home).first == GL_HANDOFF_CHILD_EXIT_127_AMBIGUOUS);
        write_script(home, HEADER + "kill -TERM $$\n");
        assert(execute(home).first == GL_HANDOFF_CHILD_SIGNALED);
        std::filesystem::remove_all(home);
    }
}

/** @return Zero after every fixed-path, same-fd, child-status, and log-routing assertion passes. */
int main() {
    test_locator_and_magic();
    test_child_results_and_log_route();
    return 0;
}
