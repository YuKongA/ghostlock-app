/* 6.1.157-android14-11-g82d681c9b06b-ab14634535 */

/* kernel_phys_load override: this firmware loads the kernel image at
 * 0xa8080000 (xbl_config FDT "Kernel" reserved region), one 0x80000 step
 * above the QC default 0xa8000000 in target.h. Without the override every
 * W1/W2 write lands 0x80000 below its target and hangs the kernel (QCOM
 * watchdog bite). Same image as the A059 JP build; symbols are identical
 * to its maintainer-verified profile. */
OFFSETS_ENTRY(
    "6.1.157-android14-11-g82d681c9b06b-ab14634535",
    STRUCT_OFFSETS_6_1,
    .kernel_phys_load = 0xa8080000,
    .pselect_waiter_shift = 1,
    .off_init_task = 0x0201f640,
    .off_init_cred = 0x02031aa8,
    .off_root_task_group = 0x02208580,
    .off_selinux_enforcing = 0x0225a420,
    .off_selinux_blob_sizes = 0x015ce8c8,
    .off_security_hook_heads = 0x015ce1b8,
    .off_slide_nfulnl_logger = 0x020129d0,
    .off_slide_boot_id = 0x0227b498,
    .off_slide_loggers_0_1 = 0x02012920,
),

/* BTF reference (runtime uses target.h defaults): */
/* #define STRUCT_PAGE_SIZE 0x40 */
/* #define STRUCT_PAGE_COMPOUND_HEAD 0x8 */
/* #define STRUCT_PAGE_TYPE 0x30 */
/* #define STRUCT_SLAB_CACHE 0x18 */
/* #define STRUCT_MM_STRUCT 0x3C0 */
