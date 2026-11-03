#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GhostLock M0 marker-only UMH fixture");

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
typedef void *(*umh_setup_t)(const char *path, char **argv, char **envp,
                             gfp_t gfp, void *init, void *cleanup, void *data);
typedef int (*umh_exec_t)(void *info, int wait);

static char glk_worker[256];
module_param_string(glk_worker, glk_worker, sizeof(glk_worker), 0400);
MODULE_PARM_DESC(glk_worker, "one per-run marker worker executable path");

/**
 * Validate the bounded absolute worker path before placing it in fixed argv.
 *
 * @param path NUL-terminated module parameter.
 * @return true for an ASCII absolute path without parent traversal.
 * @note The value is passed as one argv token; it is never interpolated into shell source.
 */
static bool m0_valid_worker_path(const char *path)
{
    size_t length;
    size_t component_start = 1;
    size_t index;

    if (path[0] != '/')
        return false;
    length = strnlen(path, sizeof(glk_worker));
    if (length < 2 || length >= sizeof(glk_worker))
        return false;
    for (index = 1; index < length; ++index) {
        const char value = path[index];
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '/' ||
              value == '.' || value == '_' || value == '-'))
            return false;
        if (value == '/') {
            if (index - component_start == 2 &&
                path[component_start] == '.' && path[component_start + 1] == '.')
                return false;
            component_start = index + 1;
        }
    }
    if (length - component_start == 2 &&
        path[component_start] == '.' && path[component_start + 1] == '.')
        return false;
    return true;
}

/**
 * Launch the fixture worker through system-root UMH without altering SELinux policy.
 *
 * @return negative errno when lookup/setup/exec fails; -ENODEV after successful helper exec unloads the fixture KO.
 * @note The shell program is constant and receives worker/run-directory as separate argv values.
 */
static int __nocfi __init m0_dfroot_init(void)
{
    static const char shell[] = "/system/bin/sh";
    static char command[] =
        "echo -n 'u:r:init:s0' > /proc/self/attr/exec; exec \"$1\" --m0-root-worker \"$2\"";
    static char run_dir[256];
    static char *environment[] = { "PATH=/system/bin", NULL };
    static char *arguments[] = { (char *)shell, "-c", command, "m0", glk_worker, run_dir, NULL };
    struct kprobe lookup_probe = { .symbol_name = "kallsyms_lookup_name" };
    kallsyms_lookup_name_t lookup;
    umh_setup_t setup;
    umh_exec_t execute;
    void *info;
    char *slash;
    int result;

    if (!m0_valid_worker_path(glk_worker)) {
        pr_err("ghostlock_m0: invalid glk_worker path\n");
        return -EINVAL;
    }
    strscpy(run_dir, glk_worker, sizeof(run_dir));
    slash = strrchr(run_dir, '/');
    if (slash == NULL || slash == run_dir) {
        pr_err("ghostlock_m0: worker has no run directory\n");
        return -EINVAL;
    }
    *slash = '\0';

    result = register_kprobe(&lookup_probe);
    if (result != 0) {
        pr_err("ghostlock_m0: kallsyms lookup probe unavailable (%d)\n", result);
        return result;
    }
    lookup = (kallsyms_lookup_name_t)lookup_probe.addr;
    unregister_kprobe(&lookup_probe);
    if ((unsigned long)lookup == 0UL) {
        pr_err("ghostlock_m0: kallsyms lookup address missing\n");
        return -EINVAL;
    }
    setup = (umh_setup_t)lookup("call_usermodehelper_setup");
    execute = (umh_exec_t)lookup("call_usermodehelper_exec");
    if ((unsigned long)setup == 0UL || (unsigned long)execute == 0UL) {
        pr_err("ghostlock_m0: UMH function missing\n");
        return -EINVAL;
    }
    info = setup(shell, arguments, environment, GFP_KERNEL, NULL, NULL, NULL);
    if (info == NULL) {
        pr_err("ghostlock_m0: UMH setup returned NULL\n");
        return -ENOMEM;
    }
    ((struct subprocess_info *)info)->path = shell;
    result = execute(info, UMH_WAIT_EXEC);
    pr_info("ghostlock_m0: UMH_WAIT_EXEC=%d selinux-unchanged=1\n", result);
    if (result != 0)
        return result;
    return -ENODEV;
}

/** Release the empty fixture module after its synchronous UMH exec handoff. */
static void __exit m0_dfroot_exit(void) {}

module_init(m0_dfroot_init);
module_exit(m0_dfroot_exit);
