/* CM-2 negative fixture: a module whose abi_version does not match the host.
 * The loader must reject it after dlopen and release the handle. */

#include "contract/abi/glk_contract_abi.h"

#include <stddef.h>

static int32_t badabi_hook(void *user, glk_stage stage,
                           const glk_contract_ops *host) {
    (void)user;
    (void)stage;
    (void)host;
    return 0;
}

static const glk_hook kHooks[] = {
    {GLK_TRIGGER_ON_STAGE, GLK_STAGE_PRE_SPAWN, 0u, 0u, badabi_hook, NULL,
     "badabi.hook"},
};

static const glk_module kModule = {
    GLK_ABI_VERSION + 1u,
    (uint32_t)sizeof(glk_module),
    "ghostlock.badabi",
    "1.0.0",
    0u,
    (uint32_t)(sizeof(kHooks) / sizeof(kHooks[0])),
    kHooks,
    /* P1 tail append: declared explicitly so the module reports the v2 size
     * with an empty schema (no params, no extract, no stage_mask). */
    0u,
    NULL,
    0u,
    NULL,
    0u,
};

const glk_module *glk_entry(uint32_t host_abi_version) {
    (void)host_abi_version;
    return &kModule;
}
