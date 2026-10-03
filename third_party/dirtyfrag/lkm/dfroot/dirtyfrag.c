#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/ptrace.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DFRoot LKM");

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
typedef void *(*umh_setup_t)(const char *path, char **argv, char **envp, gfp_t gfp,
                             void *init, void *cleanup, void *data);
typedef int (*umh_exec_t)(void *info, int wait);

static int soft_reboot;
module_param(soft_reboot, int, 0);

static int defex_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    (void)p;
    regs->regs[0] = 0;         /* x0 = DEFEX_ALLOW */
    regs->pc = regs->regs[30]; /* skip body: return to caller */
    return 1;
}

static int __nocfi __init dirtyfrag_init(void)
{
    kallsyms_lookup_name_t get_addr;
    umh_setup_t umh_setup;
    umh_exec_t  umh_exec;
    bool *selinux_state;
    struct kprobe kln_kp;
    struct kprobe defex_kp;
    struct kprobe umh_kp;
    void *info;
    int ret;

    static const char sh[]   = "/system/bin/sh";
    static const char ksud[] = "/data/user_de/0/df.root/ksud";
    static char cmd[256];
    static char *envp[] = { "PATH=/system/bin", NULL };
    static char *argv[] = { (char *)sh, "-c", cmd, NULL };
    snprintf(cmd, sizeof(cmd),
             "%s late-load --package-name me.weishu.kernelsu --ro-partitions%s"
             " && touch /dev/dfm0 || touch /dev/dfm1",
             ksud, soft_reboot ? " --soft-reboot" : "");

    kln_kp = (struct kprobe){ .symbol_name = "kallsyms_lookup_name" };
    if (register_kprobe(&kln_kp) < 0) {
        pr_err("dfroot: kallsyms_lookup_name not found\n");
        return -EINVAL;
    }
    get_addr = (kallsyms_lookup_name_t)kln_kp.addr;
    unregister_kprobe(&kln_kp);

    selinux_state = (bool *)get_addr("selinux_state");
    if (!selinux_state) {
        pr_err("dfroot: selinux_state not found\n");
        return -EINVAL;
    }
    WRITE_ONCE(*selinux_state, false);
    pr_info("dfroot: selinux_state permissive\n");

    defex_kp = (struct kprobe){ .addr = (kprobe_opcode_t *)get_addr("task_defex_enforce"),
                                .pre_handler = defex_pre_handler };
    if (register_kprobe(&defex_kp) < 0)
        pr_err("dfroot: task_defex_enforce not found\n");
    else
        pr_info("dfroot: task_defex_enforce hooked\n");

    umh_kp = (struct kprobe){ .addr = (kprobe_opcode_t *)get_addr("task_defex_user_exec"),
                              .pre_handler = defex_pre_handler };
    if (register_kprobe(&umh_kp) < 0)
        pr_err("dfroot: task_defex_user_exec not found\n");
    else
        pr_info("dfroot: task_defex_user_exec hooked\n");

    umh_setup = (umh_setup_t)get_addr("call_usermodehelper_setup");
    umh_exec  = (umh_exec_t)get_addr("call_usermodehelper_exec");
    if (!umh_setup || !umh_exec) {
        pr_err("dfroot: usermodehelper symbols missing (setup=%px exec=%px)\n",
               umh_setup, umh_exec);
        return -EINVAL;
    }

    info = umh_setup(sh, argv, envp, GFP_KERNEL, NULL, NULL, NULL);
    if (!info) {
        pr_err("dfroot: usermodehelper_setup: returned NULL\n");
        return -EINVAL;
    }
    /* bypass CONFIG_STATIC_USERMODEHELPER_PATH="" overriding path to "" */
    ((struct subprocess_info *)info)->path = sh;

    ret = umh_exec(info, UMH_WAIT_PROC);
    pr_info("dfroot: usermodehelper_exec(%s) returned %d\n", ksud, ret);

    if (defex_kp.addr) unregister_kprobe(&defex_kp);
    if (umh_kp.addr)   unregister_kprobe(&umh_kp);
    return -E2BIG; /* return any error to unload module */
}

/* no module_exit: we never unload; saves .exit sections */
module_init(dirtyfrag_init);
