/* CM-2 negative fixture: a module whose abi_version does not match the host.
 * The loader must reject it after dlopen and release the handle. */

#include "contract/glk_cm_abi.h"

#include <stddef.h>

static int32_t badabi_hook(void *user, glk_cm_stage stage,
                           const glk_host_ops *host) {
    (void)user;
    (void)stage;
    (void)host;
    return 0;
}

static const glk_cm_hook kHooks[] = {
    {GLK_CM_TRIGGER_ON_STAGE, GLK_CM_STAGE_PRE_SPAWN, 0u, 0u, badabi_hook, NULL,
     "badabi.hook"},
};

static const glk_cm_module kModule = {
    GLK_CM_ABI_VERSION + 1u,
    (uint32_t)sizeof(glk_cm_module),
    "ghostlock.badabi",
    "1.0.0",
    0u,
    (uint32_t)(sizeof(kHooks) / sizeof(kHooks[0])),
    kHooks,
};

const glk_cm_module *glk_cm_entry(uint32_t host_abi_version) {
    (void)host_abi_version;
    return &kModule;
}
