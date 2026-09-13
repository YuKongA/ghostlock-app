/* 6.1.162-android14-11-g752d9c17787d-ab15574904 */

/* Google Pixel 9 Pro (caiman) CP41.260731.005.B1 /
 * Pixel 9 Pro Fold (comet) ZP11.260717.006 CANARY.
 * Offsets extracted via vmlinux-to-elf from the boot_b kernel Image of a
 * comet running this exact kernel (kallsyms, base 0xffffffc008000000). */

OFFSETS_ENTRY(
    "6.1.162-android14-11-g752d9c17787d-ab15574904",
    STRUCT_OFFSETS_6_1,
    /* Tensor G4 (zumapro): DRAM base 0x80000000, Image text_offset=0 → the
     * kernel loads at 0x80000000 (NOT the qcom fallback 0xa8000000, which
     * produced a wrong physmap delta and a panic on first attempt). */
    .kernel_phys_load = 0x80000000,
    .pselect_waiter_shift = 1,
    .off_init_task = 0x0201f440,
    .off_init_cred = 0x020318e8,
    .off_root_task_group = 0x02209580,
    .off_selinux_enforcing = 0x0225b448,
    .off_selinux_blob_sizes = 0x015d21c8,
    .off_security_hook_heads = 0x015d1ab8,
    .off_slide_nfulnl_logger = 0x020127d0,
    .off_slide_boot_id = 0x0227c4d8,
    .off_slide_loggers_0_1 = 0x02012720,
),
