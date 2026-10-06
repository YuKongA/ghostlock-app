/* S4 P1 probe fixture: a well-formed v2 module that declares parameters and
 * extract entries plus an explicit stage_mask. Compiled as C99 to prove the
 * appended ABI is usable from a bare C toolchain. The exported call counter
 * lets the host test prove the probe never runs a hook. */

#include "contract/abi/glk_contract_abi.h"

#include <stddef.h>

static int g_hook_calls = 0;

static int32_t schema_hook(void *user, glk_stage stage,
                           const glk_contract_ops *host) {
    (void)user;
    (void)stage;
    (void)host;
    ++g_hook_calls;
    return 0;
}

static const glk_hook kHooks[] = {
    {GLK_TRIGGER_ON_STAGE, GLK_STAGE_POST_TERMINAL, 10u, 0u, schema_hook, NULL,
     "schema-hook"},
};

static const glk_param kParams[] = {
    {"threshold", GLK_PARAM_UINT, 1u, 200u, NULL, "uint parameter"},
    {"mode", GLK_PARAM_STR, 0u, 0u, "auto", "string parameter"},
    {"enabled", GLK_PARAM_BOOL, 0u, 1u, NULL, "bool parameter"},
    {"delta", GLK_PARAM_INT, 0u, (uint64_t)(int64_t)-5, NULL, NULL},
};

static const glk_param kExtract[] = {
    {"task_offset", GLK_PARAM_UINT, 1u, 0u, NULL, "extractor-provided offset"},
};

static const glk_module kModule = {
    GLK_ABI_VERSION,
    GLK_MODULE_SIZE_V2,
    "test.schema",
    "1.2.3",
    (uint32_t)(GLK_CAP_KERNEL_READ | GLK_CAP_ALIAS),
    (uint32_t)(sizeof(kHooks) / sizeof(kHooks[0])),
    kHooks,
    (uint32_t)(sizeof(kParams) / sizeof(kParams[0])),
    kParams,
    (uint32_t)(sizeof(kExtract) / sizeof(kExtract[0])),
    kExtract,
    (uint32_t)((1u << GLK_STAGE_POST_TERMINAL) | (1u << GLK_STAGE_PRE_SPAWN)),
};

const glk_module *glk_entry(uint32_t host_abi_version) {
    if (host_abi_version != GLK_ABI_VERSION) {
        return NULL;
    }
    return &kModule;
}

/* Test-only: the probe must leave this at 0. */
int glk_probe_test_hook_calls(void) { return g_hook_calls; }
