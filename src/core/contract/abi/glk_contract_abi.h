#ifndef GLK_CONTRACT_ABI_H
#define GLK_CONTRACT_ABI_H

/* GhostLock countermeasure plugin ABI, version 1 (CM-1).
 *
 * This header is the complete, stable contract between the GhostLock host
 * native executable and an out-of-tree countermeasure shared object. It is
 * deliberately self-contained C99: it includes nothing but <stdint.h> and never
 * includes another project header, so an external author can compile it with a
 * bare C toolchain and no GhostLock source tree.
 *
 * Design authority: docs/analysis/countermeasure-plugin-plan.md (sections 4, 9
 * and 10). The governing ruling is "declare the whole universe, implement the
 * minimal subset":
 *
 *   - Stages: all five are declared. The host implements PRE_SPAWN, POST_SPAWN
 *     and PRE_TERMINAL. PRE_ROUTE and POST_TERMINAL are reserved (declared but
 *     not implemented by the v1 host).
 *   - Triggers: all four are declared. The host implements only ON_STAGE.
 *     ON_LOAD, ON_BOOT_READY and PERIODIC are reserved.
 *   - Capabilities: all seven bits are declared. The host implements
 *     KERNEL_READ, KERNEL_WRITE, ALIAS and CHILD_TASK. FILE_CACHE_WRITE, EXEC
 *     and KERNEL_HOOK are reserved.
 *
 * "Reserved" does not mean best effort. A module that requires a reserved
 * trigger or capability, or hooks a reserved stage, must be rejected at
 * registration and recorded in the run diagnostics. The host must never
 * silently downgrade the requirement, ignore it, or call it anyway
 * (fail-closed; see section 9 of the plan).
 *
 * Return-code convention (the native Status convention): 0 is success and a
 * negative value is failure. When a stage function fails, the host records the
 * module and skips that module for its later stages; whether the whole attack
 * chain aborts is decided by the stage, not by this header. glk_entry()
 * returns NULL when the host ABI version is incompatible, which the host treats
 * as a fail-closed rejection.
 *
 * ABI evolution is append-only: a field may be added at the tail of an existing
 * struct only when size/abi_version can detect it. Existing field order,
 * meaning and numeric values are frozen for ABI v1.
 */

#include <stdint.h>

/* ABI version carried by the host and required in glk_module.abi_version.
 * A module with a different value is rejected before any hook is registered. */
#define GLK_ABI_VERSION 1u

/* Execution stage at which a hook runs. The ABI declares the full set; the v1
 * host implements only PRE_SPAWN, POST_SPAWN and PRE_TERMINAL.
 *
 *   PRE_SPAWN      W1 (SELinux) done, the victim is not spawned yet.
 *   POST_SPAWN     the root child exists (today's W2b); child_task is valid.
 *   PRE_TERMINAL   immediately before the terminal takes over.
 *   PRE_ROUTE      (reserved) the backend chose a route, before it runs.
 *   POST_TERMINAL  teardown, after the terminal finished. IMPLEMENTED for the
 *                  cve_2026_43284 path: it fires inside the LKM residency
 *                  window, the only interval where the kernel-side capabilities
 *                  (KernelMemory/KernelAlias over /dev/glk) exist.
 *
 * A module whose hook names PRE_ROUTE must be rejected at registration and
 * recorded. The host must not run the hook at a different stage and must not
 * silently drop only that hook. */
typedef enum glk_stage {
    GLK_STAGE_PRE_SPAWN = 0,
    GLK_STAGE_POST_SPAWN = 1,
    GLK_STAGE_PRE_TERMINAL = 2,
    GLK_STAGE_PRE_ROUTE = 3,
    GLK_STAGE_POST_TERMINAL = 4
} glk_stage;

/* What causes a hook to run. The ABI declares the full set; the v1 host
 * implements only ON_STAGE. ON_LOAD, ON_BOOT_READY and PERIODIC are reserved.
 * A module that registers a reserved trigger must be rejected at registration
 * and recorded (fail-closed); the host must not run it at load time or on a
 * best-effort schedule. */
typedef enum glk_trigger {
    GLK_TRIGGER_ON_STAGE = 0,
    GLK_TRIGGER_ON_LOAD = 1,
    GLK_TRIGGER_ON_BOOT_READY = 2,
    GLK_TRIGGER_PERIODIC = 3
} glk_trigger;

/* Host capability bits. A module ORs the bits it needs into
 * glk_module.required_caps and the host compares them with the set it
 * implements. The ABI declares all seven bits; the v1 host implements
 * KERNEL_READ, KERNEL_WRITE, ALIAS and CHILD_TASK.
 *
 *   KERNEL_READ        read kernel memory through the host primitives.
 *   KERNEL_WRITE       write kernel memory through the host primitives.
 *   ALIAS              translate an image offset to a direct-map VA.
 *   CHILD_TASK         receive the root child task in glk_contract_ops.child_task.
 *   FILE_CACHE_WRITE   (reserved) file page-cache write; not in v1.
 *   EXEC               (reserved) exec/process capability; not in v1.
 *   KERNEL_HOOK        (reserved) kernel-space hook; deliberately outside the
 *                      userspace ABI (Defex/RKP/KNOX class measures use a
 *                      separate kernel-side channel).
 *
 * A module whose required_caps contains a reserved bit must be rejected at
 * registration and recorded; the host must not clear the bit, must not load
 * the module and must not degrade to a partial capability. */
typedef enum glk_capability {
    GLK_CAP_KERNEL_READ = 1u << 0,
    GLK_CAP_KERNEL_WRITE = 1u << 1,
    GLK_CAP_ALIAS = 1u << 2,
    GLK_CAP_CHILD_TASK = 1u << 3,
    GLK_CAP_FILE_CACHE_WRITE = 1u << 4,
    GLK_CAP_EXEC = 1u << 5,
    GLK_CAP_KERNEL_HOOK = 1u << 6,
    /* Structured host logging (S4 logging batch, append-only: GLK_ABI_VERSION is
     * NOT bumped). A module that ORs this bit into required_caps declares that it
     * uses glk_contract_ops::log and expects a host that implements it; a host
     * without the bit rejects the module at registration (fail-closed).
     *
     * log() contract:
     *   - level: 0=error, 1=warn, 2=info, 3=debug. Out-of-range is mapped to 1
     *     and the host marks the line "level_clamped=1";
     *   - msg: plain text, NEVER a format string, at most 256 bytes; a longer
     *     message is truncated by the host and marked;
     *   - the host prefixes every line with "[countermeasure] <id> log(<level>): "
     *     on its diagnostic stream (stderr);
     *   - the host limits each module to 64 messages per run and to one message
     *     per millisecond; dropped messages are counted, never an error;
     *   - log() returns void and can never fail the attack chain (fail-soft). */
    GLK_CAP_LOG = 1u << 7
} glk_capability;

/* Host-provided operation surface: the only way a countermeasure touches the
 * kernel. The host sets size = sizeof(glk_contract_ops) and abi_version =
 * GLK_ABI_VERSION. ctx is an opaque host handle that the module may pass
 * back but must never dereference.
 *
 * Every operation returns 0 on success and a negative value on failure.
 * read_bytes/write_bytes move at most len bytes at a translated kernel VA.
 * zero_word writes zero through the named host primitive so the write is
 * auditable; desc may be NULL. image_to_direct_map returns 0 when the image
 * address cannot be translated. query_u64/query_str read neutral policy values
 * owned by the profile (the profile is the only enable authority; the module
 * never decides whether it is enabled). query_str writes a NUL-terminated
 * string into buf of cap bytes and returns a negative value on overflow or
 * absence. log(level, msg) records a diagnostic; msg is NUL-terminated. */
typedef struct glk_contract_ops {
    uint32_t size;
    uint32_t abi_version;
    void *ctx;

    int32_t (*read_u64)(void *ctx, uint64_t va, uint64_t *out);
    int32_t (*write_u64)(void *ctx, uint64_t va, uint64_t value);
    int32_t (*read_bytes)(void *ctx, uint64_t va, void *dst, uint32_t len);
    int32_t (*write_bytes)(void *ctx, uint64_t va, const void *src, uint32_t len);
    int32_t (*zero_word)(void *ctx, uint64_t va, const char *desc);

    uint64_t (*image_to_direct_map)(void *ctx, uint64_t image_addr);

    int32_t (*query_u64)(void *ctx, const char *path, uint64_t *out);
    int32_t (*query_str)(void *ctx, const char *path, char *buf, uint32_t cap);

    void (*log)(void *ctx, int32_t level, const char *msg);

    /* Root child task; valid from POST_SPAWN on, 0 otherwise. */
    uint64_t child_task;
} glk_contract_ops;

/* Stage callback. user is the hook's own pointer, stage is the firing stage and
 * host is the host ops for this call. Return 0 on success, negative on failure.
 * A NULL fn is invalid. */
typedef int32_t (*glk_stage_fn)(void *user, glk_stage stage, const glk_contract_ops *host);

/* One declared hook. The hook table is static and read only during load.
 *   trigger   ON_STAGE for every v1 hook.
 *   stage     meaningful when trigger == ON_STAGE.
 *   priority  smaller runs first; equal priorities keep registration order.
 *   period_ms meaningful only for the reserved PERIODIC trigger.
 *   fn        non-NULL callback.
 *   user      passed back to fn; may be NULL.
 *   name      ASCII, NUL-terminated, diagnostics only; must not be NULL. */
typedef struct glk_hook {
    glk_trigger trigger;
    glk_stage stage;
    uint32_t priority;
    uint32_t period_ms;
    glk_stage_fn fn;
    void *user;
    const char *name;
} glk_hook;

/* ------------------------------------------------------------------------- *
 * P1 append (S4 contract-design 3.14.7.3): parameter / extract schema
 *
 * Tail-only addition. GLK_ABI_VERSION stays 1: an old host ignores the new
 * tail fields and an old module simply reports the smaller size, which the
 * host detects before reading anything past the v1 fields. Nothing in the v1
 * layout above changes.
 * ------------------------------------------------------------------------- */

/* Wire type of one parameter or extract entry. The literal spelling of each
 * value matches GLKv3 WireKind (uint | int | bool | str) so the probe TSV, the
 * manifest and the Kotlin adapter share one vocabulary. */
typedef enum glk_param_type {
    GLK_PARAM_UINT = 0,
    GLK_PARAM_INT = 1,
    GLK_PARAM_BOOL = 2,
    GLK_PARAM_STR = 3
} glk_param_type;

/* One declared parameter (glk_module.params) or extract entry
 * (glk_module.extract). The table is static and read only during load.
 *   name           key under params.<name> / extract.<name>; NUL-terminated
 *   type           a glk_param_type value
 *   required       0 or 1; 1 means the profile must supply the value
 *   default_value  numeric default for uint/int/bool; ignored for STR
 *   default_str    NUL-terminated STR default, or NULL when absent
 *   doc            NUL-terminated one-line description, or NULL */
typedef struct glk_param {
    const char *name;
    uint32_t type;
    uint32_t required;
    uint64_t default_value;
    const char *default_str;
    const char *doc;
} glk_param;

/* What glk_entry() returns. The descriptor is owned by the loaded module;
 * the host reads it once during registration and never mutates it. */
typedef struct glk_module {
    uint32_t abi_version;   /* must equal the host's GLK_ABI_VERSION */
    uint32_t size;          /* = sizeof(glk_module) */
    const char *name;       /* stable id, e.g. "vivo.vr_guard" */
    const char *version;    /* module version string, diagnostics only */
    uint32_t required_caps; /* OR of glk_capability bits; reserved bit -> reject */
    uint32_t hook_count;    /* number of entries in hooks; host-bounded */
    const glk_hook *hooks; /* static table, read only after entry returns */

    /* ---- P1 tail append (S4 3.14.7.3) ----
     * Read ONLY when size >= GLK_MODULE_SIZE_V2: a module built against the v1
     * header reports the smaller v1 size, so the host must treat every field
     * below as 0 / NULL and must not dereference params or extract. */
    uint32_t param_count;      /* entries in params; 0 when none */
    const glk_param *params;   /* static table, or NULL */
    uint32_t extract_count;    /* entries in extract; 0 when none */
    const glk_param *extract;  /* static table, or NULL; P1 shape == params */
    uint32_t stage_mask;       /* OR of (1u << glk_stage) over the stages this
                                * module declares; 0 = declare nothing (the
                                * hook table still carries each hook stage) */
} glk_module;

/* sizeof(glk_module) as of the P1 append. The host compares module->size
 * against it before touching the tail fields above; modules built against the
 * v1 header report a smaller size and are read as v1 (fail-closed, never a
 * partial read). */
#define GLK_MODULE_SIZE_V2 ((uint32_t)sizeof(glk_module))

/* ------------------------------------------------------------------------- *
 * Versioned LKM request channel (delta batch)
 *
 * The out-of-tree ghostlock.ko exposes a single misc device (GLK_LKM_DEVICE_PATH)
 * and a small ioctl surface. It is the kernel-side capability provider for the
 * 43284 path: userspace forwards KernelMemory / KernelAlias operations into the
 * loaded module instead of executing plugin code in the kernel. The module must
 * only be reachable while it is resident; the native chain opens, PINGs and
 * UNLOADs it inside one short window, and an UNLOAD is the only self-unload
 * trigger (the module never stays resident on its own).
 *
 * Semantics (the native adapter and the kernel implementation must agree):
 *   abi_version  must equal GLK_LKM_ABI_VERSION for every operation; a mismatch
 *                is rejected with -EPROTO before the operation runs.
 *   PING         validates the version and reports readiness. value/len ignored.
 *   READ         addr = kernel direct-map VA, value = userspace buffer address,
 *                len = byte count (1..GLK_LKM_MAX_XFER). On success the bytes at
 *                addr are copied into the userspace buffer. status = 0 or
 *                -errno.
 *   WRITE        addr = kernel direct-map VA, value = userspace buffer address,
 *                len = byte count (1..GLK_LKM_MAX_XFER). On success the bytes
 *                from the userspace buffer are copied to addr.
 *   WRITE_ZERO   addr = kernel direct-map VA, len = byte count to zero; value is
 *                ignored. Intended for the single-word Tier 1 bootstrap and for
 *                bounded range clears.
 *   DIRECT_MAP   value = input image address; on success addr carries the
 *                direct-map alias and status = 0. A translation the kernel
 *                cannot prove returns -EFAULT rather than a guess.
 *   QUERY / LOG  reserved, currently -EOPNOTSUPP (fail-closed, never a silent 0).
 *   UNLOAD       asks the module to self-unload; the ioctl returns 0 and the
 *                module then removes itself.
 *
 * Address validation: READ / WRITE / WRITE_ZERO reject an addr (or addr+len)
 * outside the kernel direct map with -EFAULT. No authorization gate exists by
 * ruling; safety comes from the shortest possible residency window and from
 * "unloaded means Closed".
 * ------------------------------------------------------------------------- */

#define GLK_LKM_ABI_VERSION 1u
#define GLK_LKM_MAX_XFER 4096u
#define GLK_LKM_DEVICE_PATH "/dev/glk"

/* The single ioctl request number. It is a plain value (no _IOWR encoding) so
 * the header stays <stdint.h>-only; the kernel ioctl handler compares cmd
 * against it directly. The payload is always a glk_lkm_req. */
#define GLK_LKM_IOCTL 0x6747u

typedef enum glk_lkm_op {
    GLK_LKM_PING = 0,
    GLK_LKM_READ = 1,
    GLK_LKM_WRITE = 2,
    GLK_LKM_WRITE_ZERO = 3,
    GLK_LKM_DIRECT_MAP = 4,
    GLK_LKM_QUERY = 5,
    GLK_LKM_LOG = 6,
    GLK_LKM_UNLOAD = 7
} glk_lkm_op;

typedef struct glk_lkm_req {
    uint32_t abi_version; /* must == GLK_LKM_ABI_VERSION */
    uint32_t op;          /* a glk_lkm_op value */
    uint64_t addr;        /* READ/WRITE/WRITE_ZERO: target kernel direct-map VA */
    uint64_t value;       /* READ/WRITE: userspace buffer; DIRECT_MAP: image addr */
    uint32_t len;         /* READ/WRITE/WRITE_ZERO: byte count */
    uint32_t status;      /* result: 0 = ok, otherwise -errno */
} glk_lkm_req;

#ifdef __cplusplus
extern "C" {
#endif

/* The single exported symbol. host_abi_version is the host's
 * GLK_ABI_VERSION. Returns NULL when the host ABI is incompatible; the host
 * treats NULL as a fail-closed rejection. */
const glk_module *glk_entry(uint32_t host_abi_version);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GLK_CONTRACT_ABI_H */
