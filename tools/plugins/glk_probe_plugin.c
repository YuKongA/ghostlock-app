/* glk_probe_plugin: a real C-ABI plugin that exercises the LKM channel.
 *
 * Dev/gate artifact only. It is loaded by the staged entry's --plugin switch and
 * is invoked inside the LKM residency window; every call goes
 *   plugin -> glk_contract_ops -> contract capability -> LkmProxy -> ioctl -> /dev/glk
 * so a successful run proves the whole plugin->LKM path works end to end.
 *
 * Target address comes from $GLK_PLUGIN_TARGET (hex); the default is the
 * init_task alias observed on the A301SO gate device.
 */
#include "glk_contract_abi.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t target_va(void) {
    const char *env = getenv("GLK_PLUGIN_TARGET");
    if (env != NULL && env[0] != '\0') {
        return (uint64_t)strtoull(env, NULL, 16);
    }
    return 0xffffff802ac43400ULL;
}

static int32_t probe(void *user, glk_stage stage, const glk_contract_ops *host) {
    (void)user;
    if (host == NULL || host->read_u64 == NULL || host->read_bytes == NULL ||
        host->write_bytes == NULL || host->log == NULL) {
        return -1;
    }
    const uint64_t va = target_va();
    uint64_t value = 0;
    uint8_t buf[8] = {0};
    const int32_t rc_read = host->read_u64(host->ctx, va, &value);
    const int32_t rc_bytes = host->read_bytes(host->ctx, va, buf, 8U);
    int32_t rc_write = -1;
    if (rc_bytes == 0) {
        /* Idempotent write-back: same bytes, so the target is unchanged. */
        rc_write = host->write_bytes(host->ctx, va, buf, 8U);
    }
    /* Out-of-range must be rejected by the kernel (address legality). */
    uint64_t bogus = 0;
    const int32_t rc_bad = host->read_u64(host->ctx, 0xdead000000000000ULL, &bogus);

    char msg[176];
    (void)snprintf(msg, sizeof msg,
                   "glk.probe stage=%u va=0x%llx val=0x%llx read=%d bytes=%d write=%d "
                   "bad_rejected=%d",
                   (unsigned)stage, (unsigned long long)va, (unsigned long long)value,
                   (int)rc_read, (int)rc_bytes, (int)rc_write, (int)(rc_bad != 0));
    host->log(host->ctx, 1, msg);

    return (rc_read == 0 && rc_bytes == 0 && rc_write == 0 && rc_bad != 0) ? 0 : -1;
}

static const glk_hook k_hooks[] = {
    {GLK_TRIGGER_ON_STAGE, GLK_STAGE_POST_TERMINAL, 0U, 0U, probe, NULL, "glk.probe"},
};

static const glk_module k_module = {
    GLK_ABI_VERSION, (uint32_t)sizeof(glk_module), "glk.probe", "1.0", 0U, 1U, k_hooks,
};

/* The host passes its ABI version; an incompatible host gets NULL, which the
 * loader treats as a fail-closed rejection. */
const glk_module *glk_entry(uint32_t host_abi_version) {
    if (host_abi_version != GLK_ABI_VERSION) {
        return NULL;
    }
    return &k_module;
}
