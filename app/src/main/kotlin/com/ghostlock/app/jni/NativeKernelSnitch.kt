package com.ghostlock.app.jni

/**
 * KernelSnitch 的 native 边界（`libghostlock_general.so`）。
 *
 * 只做句柄与原始返回值的传递；扫描范围、线程数、碰撞数、绑核全部由调用方给出。
 * 本对象不保存任何状态，句柄由调用方持有并保证 `create`/`destroy` 各一次。
 *
 * @note 所有调用都是阻塞的：`findCollisions` 扫描整张 futex hash 表，
 *       `bruteforce` 最多遍历 64 GiB 内核区间。必须在后台线程调用，
 *       且同一进程内不要并发调用。
 * @note `findCollisions` 会为每个碰撞桶创建约 1024 个等待 futex 的 detached
 *       线程；成功路径会唤醒，失败路径可能残留到进程退出。这是 legacy
 *       KernelSnitch 的行为，拆解前不要在生产进程里反复调用。
 * @note 句柄是 KernelSnitch 共享状态的地址，只能在 `libghostlock_general.so`
 *       内解读；不要跨进程、不要序列化。
 */
internal object NativeKernelSnitch {
    init {
        System.loadLibrary("ghostlock_general")
    }

    /**
     * 创建一次 KernelSnitch 运行状态。
     *
     * @param mmStructSize  `sizeof(mm_struct)`，爆破步长。
     * @param mmSlabOrder   mm_struct slab order。
     * @param threadCount   爆破线程数。
     * @param collisionCount 需要建立的哈希碰撞数，至少 2。
     * @param verbose       是否输出扫描细节。
     * @param core          绑定的 CPU；传 -1 表示不绑定。
     * @return 非 0 句柄；参数非法或初始化失败返回 0。
     */
    external fun create(
        mmStructSize: Long,
        mmSlabOrder: Long,
        threadCount: Long,
        collisionCount: Long,
        verbose: Boolean,
        core: Int,
    ): Long

    /**
     * 执行一次碰撞扫描（阻塞）。
     *
     * @param handle [create] 返回的句柄。
     * @return 是否找齐所需碰撞；句柄无效或阶段不对返回 false。
     */
    external fun findCollisions(handle: Long): Boolean

    /**
     * 在碰撞已建立后爆破 mm_struct 地址（阻塞）。
     *
     * @param handle [create] 返回的句柄。
     * @return mm_struct 地址；未找到或前提不满足返回 -1。
     */
    external fun bruteforce(handle: Long): Long

    /**
     * 查询当前阶段，用于记录失败原因。
     *
     * @param handle [create] 返回的句柄。
     * @return `kernelsnitch_state` 的枚举值；句柄为 0 时返回 0
     *         （`KERNELSNITCH_NOT_INIT`）。
     */
    external fun state(handle: Long): Int

    /**
     * 回收一次 KernelSnitch 运行状态。
     *
     * @param handle [create] 返回的句柄；0 允许。
     * @return 本次运行找到的 mm_struct；未找到返回 -1。
     * @note 必须恰好调用一次；之后句柄失效。
     */
    external fun destroy(handle: Long): Long

    /**
     * 一次完成 setup → 碰撞 → 爆破 → cleanup 的便捷入口。
     *
     * @return mm_struct 地址；参数非法或未找到返回 -1。
     * @note 与 [create]/[findCollisions]/[bruteforce]/[destroy] 等价，
     *       供首轮接线使用；拆解完成后应改走分阶段入口。
     */
    external fun run(
        mmStructSize: Long,
        mmSlabOrder: Long,
        threadCount: Long,
        collisionCount: Long,
        verbose: Boolean,
        core: Int,
    ): Long
}
