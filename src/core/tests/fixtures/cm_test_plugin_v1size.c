/* S4 P1 probe fixture: a module that reports the v1 glk_module size while the
 * physical struct carries the P1 tail. The tail fields are poisoned on purpose:
 * a host that reads them without the size gate would print bogus params or
 * dereference a garbage pointer. */

#include "contract/abi/glk_contract_abi.h"

#include <stddef.h>

static int32_t v1_hook(void *user, glk_stage stage,
                       const glk_contract_ops *host) {
    (void)user;
    (void)stage;
    (void)host;
    return 0;
}

static const glk_hook kHooks[] = {
    {GLK_TRIGGER_ON_STAGE, GLK_STAGE_PRE_SPAWN, 0u, 0u, v1_hook, NULL, "v1.hook"},
};

static const glk_module kModule = {
    GLK_ABI_VERSION,
    (uint32_t)offsetof(glk_module, param_count), /* v1 size: tail is invisible */
    "test.v1size",
    "0.9.0",
    0u,
    (uint32_t)(sizeof(kHooks) / sizeof(kHooks[0])),
    kHooks,
    0xFFFFFFFFu,                    /* poisoned param_count */
    (const glk_param *)(size_t)0x1, /* poisoned params */
    0xFFFFFFFFu,                    /* poisoned extract_count */
    (const glk_param *)(size_t)0x1, /* poisoned extract */
    0xFFFFFFFFu,                    /* poisoned stage_mask */
};

const glk_module *glk_entry(uint32_t host_abi_version) {
    if (host_abi_version != GLK_ABI_VERSION) {
        return NULL;
    }
    return &kModule;
}
