/**
 * @brief Harmless exec fixture that consumes stdin and reports only byte counts.
 * @param argc Number of argv entries, including executable name.
 * @param argv Argument vector; values are intentionally never printed.
 * @return Zero after stdin reaches EOF; nonzero only when a read or write fails.
 * @note This fixture does not open files, create processes, access sockets, or invoke syscalls beyond read/write.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#ifndef FIXTURE_BACKEND
#error FIXTURE_BACKEND must name the backend identity
#endif

int main(int argc, char **argv) {
    (void)argv;
    uint64_t consumed = 0U;
    char buffer[4096];
    for (;;) {
        ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count == 0) {
            break;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 2;
        }
        consumed += (uint64_t)count;
    }
    if (printf("fixture=ghostlock-exec-noop-v1\nbackend=" FIXTURE_BACKEND "\nargv_count=%d\nstdin_bytes=%llu\n",
               argc - 1, (unsigned long long)consumed) < 0) {
        return 3;
    }
    return fflush(stdout) == 0 ? 0 : 4;
}
