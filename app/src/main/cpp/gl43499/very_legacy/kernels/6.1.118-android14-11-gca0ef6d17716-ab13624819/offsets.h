/* 6.1.118-android14-11-gca0ef6d17716-ab13624819: Xiaomi 14 (houji), SM8650,
 * HyperOS 3.0.3.0.
 * Anchors from the boot.img (kallsyms + BTF). */

OFFSETS_ENTRY(
    "6.1.118-android14-11-gca0ef6d17716-ab13624819",
    STRUCT_OFFSETS_6_1,
    .pselect_waiter_shift = 1,
    .off_init_task = 0x01fdf600,
    .off_init_cred = 0x01ff1a68,
    .off_root_task_group = 0x021c6580,
    .off_selinux_enforcing = 0x022183d0,
    .off_selinux_blob_sizes = 0x015b3a48,
    .off_security_hook_heads = 0x015b3338,
    .off_slide_nfulnl_logger = 0x01fd29c8,
    .off_slide_boot_id = 0x02239458,
    .off_slide_loggers_0_1 = 0x01fd2918,
),

/* BTF reference for fields kept as target.h macros; the task, cred and
 * pi field offsets ride STRUCT_OFFSETS_6_1 at runtime: */
/* #define STRUCT_PAGE_SIZE 0x40 */
/* #define STRUCT_PAGE_COMPOUND_HEAD 0x8 */
/* #define STRUCT_PAGE_TYPE 0x30 */
/* #define STRUCT_SLAB_CACHE 0x18 */
/* #define STRUCT_MM_STRUCT 0x3C0 */
