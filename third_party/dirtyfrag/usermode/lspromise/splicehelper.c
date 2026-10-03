#include <syscall.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

// aarch64-linux-android30-clang splicehelper.c -o splicehelper -nodefaultlibs -nostartfiles -ffreestanding -static

#define OUT_FD 0
// the pipe fd used to splice
#define PIPE_FD 1

__attribute__((naked)) static long mysyscall1(
    unsigned long arg0,
    unsigned long nr
) {
    asm volatile(
        "mov x8, x1\n"
        "svc 0\n"
        "ret\n":::"x8"
        );
}

__attribute__((naked)) static long mysyscall3(
    unsigned long arg0, unsigned long arg1, unsigned long arg2,
    unsigned long nr
) {
    asm volatile(
        "mov x8, x3\n"
        "svc 0\n"
        "ret\n":::"x8"
        );
}

__attribute__((naked)) static long mysyscall6(
    unsigned long arg0, unsigned long arg1, unsigned long arg2,
    unsigned long arg3, unsigned long arg4, unsigned long arg5, unsigned long nr
) {
    asm volatile(
        "mov x8, x6\n"
        "svc 0\n"
        "ret\n":::"x8"
        );
}

static unsigned long parse_int(char *s) {
    unsigned long val = 0;
    while (*s) {
        val *= 10;
        val += (*s - '0');
        s++;
    }
    return val;
}

#define DEBUG_SPLICE_HELPER 0

void start_c(void* argblock) {
    off64_t off = parse_int(*(((char**) argblock)+2));
    int file_fd = mysyscall3(
        (unsigned long) AT_FDCWD,
        (unsigned long) "/vendor/lib64/libstagefright_aidl_bufferpool2.so",
        (unsigned long) O_RDONLY,
        __NR_openat
    );
    if (file_fd < 0) {
#if DEBUG_SPLICE_HELPER
        mysyscall3(
            OUT_FD,
            (unsigned long)"open fd",
             sizeof("open fd"),
            __NR_write
        );
        mysyscall3(
            OUT_FD,
            (unsigned long) &file_fd,
             sizeof(file_fd),
            __NR_write
        );
#endif
        mysyscall1(1, __NR_exit_group);
    }
    unsigned long ret = mysyscall6(
        file_fd,
        (unsigned long) &off,
        PIPE_FD, (unsigned long) NULL, 16, SPLICE_F_MOVE, __NR_splice);
    if (ret != 16) {
#if DEBUG_SPLICE_HELPER
        mysyscall3(
            OUT_FD,
            (unsigned long)"splice",
             sizeof("splice"),
            __NR_write
        );
        mysyscall3(
            OUT_FD,
            (unsigned long) &ret,
             sizeof(ret),
            __NR_write
        );
#endif
    }
    mysyscall1(ret-16, __NR_exit_group);
}

__attribute__((naked)) void _start() {
    asm(
        ".extern start_c\n"
        "mov x0, sp\n"
        "b start_c\n"
        );
}
