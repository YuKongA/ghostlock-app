#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <err.h>
#include <cstdio>
#include <sys/mman.h>
#include <sched.h>
#include <ctime>
#include <cstring>
#include <sys/resource.h>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <cinttypes>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/prctl.h>

#include "number_parse.h"
#include "runtime_time.h"
#include "support/native_resource.hpp"

#ifdef ANDROID_APP_NO_LKM
#include <android/log.h>
#endif

#ifndef HIDEMINMAX
#define MAX(X,Y) (((X) > (Y)) ? (X) : (Y))
#define MIN(X,Y) (((X) < (Y)) ? (X) : (Y))
#endif

// #define DEBUG
// #define PANIC

#define COLOR_GREEN "\033[32m"
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_DEFAULT "\033[0m"

#define SYSCHK(x) ({ \
        __typeof__(x) __res = (x); \
        if (__res == (__typeof__(x))-1) \
            pr_error("SYSCHK(" #x "): %m\n"); \
        __res; \
    })
#define SYSCHK_pr(x, fmt) ({ \
        __typeof__(x) __res = (x); \
        if (__res == (__typeof__(x))-1) \
            pr_error(fmt); \
        __res; \
    })

#ifdef PANIC
#define PR_ASSERT pr_error
#else
#define PR_ASSERT pr_warning
#endif

#define ASSERT(cond) do { \
        if (!!(cond) == 0) \
            PR_ASSERT("[detected] assert(" #cond ")\n"); \
    } while (0)
#define ASSERT_pr(cond, fmt, ...) do { \
        if (!!(cond) == 0) \
            PR_ASSERT("[detected] assert(%s): " fmt, #cond, ##__VA_ARGS__); \
    } while (0)

#ifdef DEBUG
#ifdef ANDROID_APP_NO_LKM
#define pr_error(fmt, ...) do { \
        __android_log_print(ANDROID_LOG_ERROR, "google_poc_app", "[!] %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
        exit(-1); \
    } while (0)
#define pr_warning(fmt, ...) do { \
        __android_log_print(ANDROID_LOG_WARN, "google_poc_app", "[-] %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#define pr_info(fmt, ...) do { \
        __android_log_print(ANDROID_LOG_INFO, "google_poc_app", "[*] %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#define pr_success(fmt, ...) do { \
        __android_log_print(ANDROID_LOG_INFO, "google_poc_app", "[+] %s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#else
#define pr_error(fmt, ...) do { \
        printf(COLOR_RED "[!] %s:%d " COLOR_DEFAULT fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
        exit(-1); \
    } while (0)
#define pr_warning(fmt, ...) do { \
        printf(COLOR_RED "[-] %s:%d " COLOR_DEFAULT fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#define pr_info(fmt, ...) do { \
        printf(COLOR_YELLOW "[*] %s:%d " COLOR_DEFAULT fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#define pr_success(fmt, ...) do { \
        printf(COLOR_GREEN "[+] %s:%d " COLOR_DEFAULT fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#endif
#else
#ifdef ANDROID_APP_NO_LKM
#define pr_error(fmt, ...) do { \
        __android_log_print(ANDROID_LOG_ERROR, "google_poc_app", "[!] " fmt, ##__VA_ARGS__); \
        exit(-1); \
    } while (0)
#define pr_warning(fmt, ...) do { \
        __android_log_print(ANDROID_LOG_WARN, "google_poc_app", "[-] " fmt, ##__VA_ARGS__); \
    } while (0)
#define pr_info(fmt, ...) do { \
        __android_log_print(ANDROID_LOG_INFO, "google_poc_app", "[*] " fmt, ##__VA_ARGS__); \
    } while (0)
#define pr_success(fmt, ...) do { \
        __android_log_print(ANDROID_LOG_INFO, "google_poc_app", "[+] " fmt, ##__VA_ARGS__); \
    } while (0)
#else
#define pr_error(fmt, ...) do { \
        printf(COLOR_RED "[!] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
        exit(-1); \
    } while (0)
#define pr_warning(fmt, ...) do { \
        printf(COLOR_RED "[-] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
    } while (0)
#define pr_info(fmt, ...) do { \
        printf(COLOR_YELLOW "[*] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
    } while (0)
#define pr_success(fmt, ...) do { \
        printf(COLOR_GREEN "[+] " COLOR_DEFAULT fmt, ##__VA_ARGS__); \
    } while (0)
#endif
#endif

#define wait_input(fmt, ...) do { \
        pr_info(fmt, ##__VA_ARGS__); getchar(); \
    } while (0)

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/* CPP02 review: SYSCHK is log-and-continue (it reports the error and returns
 * -1); callers have no better recovery than proceeding, so the form is kept. */
namespace ghostlock::kernel {

static inline void pin_to_core(size_t core) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    SYSCHK(sched_setaffinity(0, sizeof(cpu_set_t), &cpuset));
}

static inline void reset_cpu_pin(void) {
    cpu_set_t cpuset;
    memset(&cpuset, 0xff, sizeof(cpu_set_t));
    SYSCHK(sched_setaffinity(0, sizeof(cpu_set_t), &cpuset));
}

/* CPP02 review: same log-and-continue retention as pin_to_core(). */
static inline void set_limit(void) {
    struct rlimit r;
    SYSCHK(getrlimit(RLIMIT_NOFILE, &r));
    r.rlim_cur = r.rlim_max;
    SYSCHK(setrlimit(RLIMIT_NOFILE, &r));
    SYSCHK(getrlimit(RLIMIT_NPROC, &r));
    r.rlim_cur = r.rlim_max;
    SYSCHK(setrlimit(RLIMIT_NPROC, &r));
}

static inline void set_unbuffer(void) {
    SYSCHK(setvbuf(stdin, nullptr, _IONBF, 0));
    SYSCHK(setvbuf(stdout, nullptr, _IONBF, 0));
    SYSCHK(setvbuf(stderr, nullptr, _IONBF, 0));
}

static inline void set_proc_name(const char *name) {
    SYSCHK(prctl(PR_SET_NAME, name, 0, 0, 0));
}

static inline size_t gettime_ns(void) {
    return static_cast<size_t>(ghostlock::runtime_time::to_duration(
        ghostlock::runtime_time::monotonic_now()).count());
}

static void write_file(const char *path, const char *data) {
    ghostlock::support::UniqueFd fd(SYSCHK(open(path, O_WRONLY)));
    const size_t length = strlen(data);
    if (write(fd.get(), data, length) != (ssize_t) length)
        pr_error("write(%s): %m\n", path);
}


static inline void hexdump(const void *data, size_t size) {
    char ascii[17];
    size_t i, j;
    ascii[16] = '\0';
    for (i = 0; i < size; ++i) {
        printf("%02X ", ((unsigned char *) data)[i]);
        if (((unsigned char *) data)[i] >= ' ' && ((unsigned char *) data)[i] <= '~') {
            ascii[i % 16] = ((unsigned char *) data)[i];
        } else {
            ascii[i % 16] = '.';
        }
        if ((i + 1) % 8 == 0 || i + 1 == size) {
            printf(" ");
            if ((i + 1) % 16 == 0) {
                printf("|  %s \n", ascii);
            } else if (i + 1 == size) {
                ascii[(i + 1) % 16] = '\0';
                if ((i + 1) % 16 <= 8) {
                    printf(" ");
                }
                for (j = (i + 1) % 16; j < 16; ++j) {
                    printf("   ");
                }
                printf("|  %s \n", ascii);
            }
        }
    }
}

static inline unsigned long parse_ul(const char *s, const char *name) {
    const kernelsnitch::ParsedUnsigned parsed = kernelsnitch::number_parse_unsigned(s, 0);
    if (!parsed.valid)
        pr_error("invalid %s: %s\n", name, s);
    return parsed.value;
}

static inline unsigned long parse_xl(const char *s, const char *name) {
    const kernelsnitch::ParsedUnsigned parsed = kernelsnitch::number_parse_unsigned(s, 16);
    if (!parsed.valid)
        pr_error("invalid %s: %s\n", name, s);
    return parsed.value;
}

} // namespace ghostlock::kernel
