/* S4 P1 probe audit fixtures (D4 + D7).
 *
 * The module is otherwise well-formed, but it carries two things the loader
 * would reject at registration and the probe must therefore NOT publish as if
 * they were valid:
 *   - a hook whose name is NULL (a required column)  -> no hook row, reject row;
 *   - a parameter whose name contains a control byte -> no param row, reject row.
 * Consumers reject a malformed row before they ever reach the reject line, so
 * publishing it would hide the real reason (audit D4). */

#include "contract/abi/glk_contract_abi.h"

#include <stddef.h>

static int32_t nullhook_hook(void *user, glk_stage stage,
                             const glk_contract_ops *host) {
    (void)user;
    (void)stage;
    (void)host;
    return 0;
}

static const glk_hook kHooks[] = {
    {GLK_TRIGGER_ON_STAGE, GLK_STAGE_PRE_SPAWN, 0u, 0u, nullhook_hook, NULL, NULL},
};

static const glk_param kParams[] = {
    {"bad\x01name", GLK_PARAM_UINT, 0u, 1u, NULL, "control byte in name"},
};

static const glk_module kModule = {
    GLK_ABI_VERSION,
    GLK_MODULE_SIZE_V2,
    "demo.nullhook",
    "1.0.0",
    0u,
    (uint32_t)(sizeof(kHooks) / sizeof(kHooks[0])),
    kHooks,
    (uint32_t)(sizeof(kParams) / sizeof(kParams[0])),
    kParams,
    0u,
    NULL,
    0u,
};

const glk_module *glk_entry(uint32_t host_abi_version) {
    if (host_abi_version != GLK_ABI_VERSION) {
        return NULL;
    }
    return &kModule;
}
