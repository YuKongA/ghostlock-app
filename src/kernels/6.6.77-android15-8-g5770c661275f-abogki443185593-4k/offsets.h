/* 6.6.77-android15-8-g5770c661275f-abogki443185593-4k
 * XRing O1 (Xiaomi Pad 7 Ultra / 玄戒 O1) companion kernel build.
 * Values regenerated from the OS3.0.303.0.WOTCNXM full-OTA boot.img by the
 * upstream XRING O1 tree (ayyy7128/ghostlock-xringo1, now offline) and
 * verified on-device against its prebuilt libghostlock.so profile.
 * This is the second validated XRing O1 kernel; the sibling
 * 6.6.118-android15-8-gc44b714366cc-abogki519650608-4k is already in this
 * tree via PR #7. */

OFFSETS_ENTRY(
    "6.6.77-android15-8-g5770c661275f-abogki443185593-4k",
    STRUCT_OFFSETS_6_6,
    .pselect_waiter_shift = -2,
    .off_init_task = 0x020de280,
    .off_init_cred = 0x020f0548,
    .off_root_task_group = 0x022d4580,
    .off_selinux_enforcing = 0x02315f68,
    .off_selinux_blob_sizes = 0x0164fb48,
    .off_security_hook_heads = 0x0164f410,
    .off_slide_nfulnl_logger = 0x020d2270,
    .off_slide_boot_id = 0x02336f58,
    .off_slide_loggers_0_1 = 0x020d21c8,
),

/* BTF reference (runtime uses target.h defaults): */
/* #define STRUCT_PAGE_SIZE 0x40 */
/* #define STRUCT_PAGE_COMPOUND_HEAD 0x8 */
/* #define STRUCT_PAGE_TYPE 0x30 */
/* #define STRUCT_SLAB_CACHE 0x8 */
/* #define STRUCT_MM_STRUCT 0x4C0 */
