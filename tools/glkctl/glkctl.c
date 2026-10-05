/* glkctl: exercise the ghostlock resident LKM request channel (/dev/glk).
 *
 * Dev/gate tool only. Usage:
 *   glkctl ping    <abi_version>
 *   glkctl read    <addr_hex> [len]
 *   glkctl write   <addr_hex> [len]      (idempotent: reads then writes back)
 *   glkctl zerofail <addr_hex>           (expect -EFAULT)
 *   glkctl unload
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define GLK_LKM_IOCTL 0x6747u
#define GLK_LKM_MAX_XFER 4096u
enum { GLK_LKM_PING = 0, GLK_LKM_READ = 1, GLK_LKM_WRITE = 2, GLK_LKM_WRITE_ZERO = 3,
       GLK_LKM_DIRECT_MAP = 4, GLK_LKM_QUERY = 5, GLK_LKM_LOG = 6, GLK_LKM_UNLOAD = 7 };

struct glk_lkm_req {
    uint32_t abi_version;
    uint32_t op;
    uint64_t addr;
    uint64_t value;
    uint32_t len;
    uint32_t status;
};

static int call(int fd, struct glk_lkm_req *r) {
    errno = 0;
    int rc = ioctl(fd, GLK_LKM_IOCTL, r);
    return rc < 0 ? -errno : (int)r->status;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: glkctl <ping|read|write|zerofail|unload> [args]\n"); return 2; }
    int fd = open("/dev/glk", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open /dev/glk"); return 1; }
    struct glk_lkm_req r;
    memset(&r, 0, sizeof r);
    const char *cmd = argv[1];

    if (strcmp(cmd, "ping") == 0) {
        r.abi_version = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 1u;
        int rc = call(fd, &r);
        printf("ping abi=%u rc=%d status=%u\n", r.abi_version, rc, r.status);
        return rc == 0 ? 0 : 3;
    }
    if (strcmp(cmd, "seq") == 0) {
        /* Full window in ONE process: ping -> read -> idempotent write -> UNLOAD,
         * then the fd close. Exercises the explicit (fast) unload path. */
        const char *hex = argc > 2 ? argv[2] : "ffffff802ac43400";
        uint64_t addr = strtoull(hex, NULL, 16);
        uint8_t buf[8];
        memset(&r, 0, sizeof r);
        r.abi_version = 1u; r.op = GLK_LKM_PING;
        printf("seq ping rc=%d\n", call(fd, &r));
        memset(&r, 0, sizeof r);
        r.abi_version = 1u; r.op = GLK_LKM_READ; r.addr = addr; r.value = (uint64_t)(uintptr_t)buf; r.len = 8u;
        int rc = call(fd, &r);
        printf("seq read rc=%d data=%02x%02x%02x%02x%02x%02x%02x%02x\n", rc,
               buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
        memset(&r, 0, sizeof r);
        r.abi_version = 1u; r.op = GLK_LKM_WRITE; r.addr = addr; r.value = (uint64_t)(uintptr_t)buf; r.len = 8u;
        printf("seq write rc=%d\n", call(fd, &r));
        memset(&r, 0, sizeof r);
        r.abi_version = 1u; r.op = GLK_LKM_UNLOAD;
        printf("seq unload rc=%d\n", call(fd, &r));
        printf("seq done pid=%d (fd still open; close follows)\n", (int)getpid());
        return 0;
    }
    if (strcmp(cmd, "hold") == 0) {
        /* Open the channel and sleep while KEEPING the fd: simulates a live
         * client session. kill -9 on this process must end the LKM window. */
        uint32_t secs = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 0) : 30u;
        printf("hold fd=%d secs=%u pid=%d (kill -9 to test crash path)\n", fd, secs, (int)getpid());
        fflush(stdout);
        sleep(secs);
        return 0;
    }
    if (strcmp(cmd, "unload") == 0) {
        r.abi_version = 1u; r.op = GLK_LKM_UNLOAD;
        int rc = call(fd, &r);
        printf("unload rc=%d status=%u\n", rc, r.status);
        return rc == 0 ? 0 : 3;
    }
    if (strcmp(cmd, "read") == 0 || strcmp(cmd, "write") == 0) {
        if (argc < 3) { fprintf(stderr, "need addr\n"); return 2; }
        uint64_t addr = strtoull(argv[2], NULL, 16);
        uint32_t len = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 0) : 8u;
        if (len == 0 || len > GLK_LKM_MAX_XFER) { fprintf(stderr, "bad len\n"); return 2; }
        uint8_t buf[GLK_LKM_MAX_XFER];
        memset(&r, 0, sizeof r);
        r.abi_version = 1u; r.op = GLK_LKM_READ; r.addr = addr; r.value = (uint64_t)(uintptr_t)buf; r.len = len;
        int rc = call(fd, &r);
        printf("read  addr=0x%llx len=%u rc=%d status=%u", (unsigned long long)addr, len, rc, r.status);
        if (rc == 0) { printf(" data="); for (uint32_t i = 0; i < len && i < 16u; ++i) printf("%02x", buf[i]); }
        printf("\n");
        if (rc != 0 || strcmp(cmd, "read") == 0) return rc == 0 ? 0 : 3;
        memset(&r, 0, sizeof r);
        r.abi_version = 1u; r.op = GLK_LKM_WRITE; r.addr = addr; r.value = (uint64_t)(uintptr_t)buf; r.len = len;
        int wrc = call(fd, &r);
        printf("write addr=0x%llx len=%u rc=%d status=%u (idempotent same-bytes)\n",
               (unsigned long long)addr, len, wrc, r.status);
        return wrc == 0 ? 0 : 3;
    }
    if (strcmp(cmd, "zerofail") == 0) {
        if (argc < 3) { fprintf(stderr, "need addr\n"); return 2; }
        r.abi_version = 1u; r.op = GLK_LKM_WRITE_ZERO; r.addr = strtoull(argv[2], NULL, 16); r.len = 8u;
        int rc = call(fd, &r);
        printf("zerofail addr=0x%s rc=%d status=%u (expect -EFAULT)\n", argv[2], rc, r.status);
        return 0;
    }
    fprintf(stderr, "unknown cmd %s\n", cmd);
    return 2;
}
