/** @file terminal/execution.cpp
 *  Starting the finishing payload, and nothing else.
 */
#include "terminal/execution.hpp"

#include <cstring>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace ghostlock::terminal {
    ExecResult run_payload(const char *path) noexcept {
        ExecResult result{};
        if (path == nullptr || path[0] == '\0') {
            return result;
        }
        struct stat info{};
        if (::stat(path, &info) != 0 || !S_ISREG(info.st_mode)) {
            return result;
        }
        /* The store writes 0600; running is the one place that widens it, and
         * only to the owner-executable bit. */
        if ((info.st_mode & S_IXUSR) == 0) {
            if (::chmod(path, 0700) != 0) {
                return result;
            }
        }
        const pid_t child = ::fork();
        if (child < 0) {
            return result;
        }
        if (child == 0) {
            /* The child runs the payload and nothing else: no shell, no
             * arguments, the environment as inherited. */
            char *const argv[] = {const_cast<char *>(path), nullptr};
            ::execve(path, argv, environ);
            ::_exit(127);
        }
        int status = 0;
        while (::waitpid(child, &status, 0) < 0) {
            /* A signal during the wait is not the payload's outcome. */
        }
        if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
            result.ok = true;
            result.reason = "ok";
            return result;
        }
        /* Killed by a signal: the payload did not finish its work. */
        result.exit_code = -1;
        return result;
    }
}  // namespace ghostlock::terminal
