/* 6.6.89-android15-8-g97a9aaefab9a-ab14519050-4k */

OFFSETS_ENTRY(
    "6.6.89-android15-8-g97a9aaefab9a-ab14519050-4k",
    STRUCT_OFFSETS_6_6,
    .pselect_waiter_shift = -2,
    .off_init_task = 0x020fe280,
    .off_init_cred = 0x02110548,
    .off_root_task_group = 0x022f5580,
    .off_selinux_enforcing = 0x02336ea0,
    .off_selinux_blob_sizes = 0x016626f0,
    .off_security_hook_heads = 0x01661fb8,
    .off_slide_nfulnl_logger = 0x020f2258,
    .off_slide_boot_id = 0x02357e98,
    .off_slide_loggers_0_1 = 0x020f21a8,
    /* __tracepoint_sys_exit @ 0xffffffc082292120 in the disassembled GKI
     * vmlinux (adrp x8, 0xffffffc082292000 + 0x120) -> image offset
     * 0x02292120. Image-relative, like the off_* values above.
     * Needed for vivo vr.ko neutralization; only acted on when vr.ko is
     * present in /proc/modules, so non-vivo devices are unaffected. */
    .off_vr_sys_exit_tp = 0x02292120,
),

/* BTF reference (runtime uses target.h defaults): */
/* #define STRUCT_PAGE_SIZE 0x40 */
/* #define STRUCT_PAGE_COMPOUND_HEAD 0x8 */
/* #define STRUCT_PAGE_TYPE 0x30 */
/* #define STRUCT_SLAB_CACHE 0x8 */
/* #define STRUCT_MM_STRUCT 0x500 */
