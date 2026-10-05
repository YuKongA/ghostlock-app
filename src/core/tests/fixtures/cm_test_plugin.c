/* CM-2 positive countermeasure test plugin, compiled as C99.
 *
 * It proves the ABI header (contract/abi/glk_contract_abi.h) is usable from a bare C
 * toolchain and that the loader accepts a well-formed module. It declares one
 * ON_STAGE/PRE_SPAWN hook and counts invocations so the host test can prove the
 * loader never calls a hook (dispatch is CM-3). */

#include "contract/abi/glk_contract_abi.h"

#include <stddef.h>

static int g_invocations = 0;

static int32_t test_hook(void *user, glk_stage stage,
                         const glk_contract_ops *host) {
    (void)user;
    (void)stage;
    (void)host;
    ++g_invocations;
    return 0;
}

static const glk_hook kHooks[] = {
    {GLK_TRIGGER_ON_STAGE, GLK_STAGE_PRE_SPAWN, 0u, 0u, test_hook, NULL,
     "test.pre_spawn"},
};

static const glk_module kModule = {
    GLK_ABI_VERSION,
    (uint32_t)sizeof(glk_module),
    "ghostlock.test",
    "1.0.0",
    (uint32_t)(GLK_CAP_KERNEL_READ | GLK_CAP_ALIAS),
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
    if (host_abi_version != GLK_ABI_VERSION) {
        return NULL;
    }
    return &kModule;
}

/* Test-only probe: the loader must leave this at 0. */
int glk_test_invocations(void) { return g_invocations; }
