package com.ghostlock.app.core.victim

/**
 * Native 通用 victim transport seam（批 A：只定义接口；JNI/CMake 接线在批 B）。
 *
 * 生产实现见 [VictimNativeJni]（批 B1），1:1 包装 `app/src/main/cpp/gl43499/core/victim_protocol.{c,h}`：
 * posix_spawn helper、file_actions（control=3/raw=4）、close_range、pipe/memfd framing、
 * waitpid/超时、kill/reap。**Native 不解析帧内容**（只做流边界/上限），因此本 seam
 * 的 `frame` 参数是 Kotlin [VictimProtocol] 生成的完整线帧。
 *
 * @note 所有方法必须不以异常表达业务失败（返回 [VictimResult]）；本 seam 只表达
 *   「机械调用发生了什么」，不表达 victim 业务状态。
 * @note 同一 handle 上的调用由 [VictimSession] 串行化；实现无需可重入。
 * @note 阶段：批 A（task-52）；不接 Flow/JNI 生产路径。
 */
internal interface VictimNative {
    /**
     * posix_spawn 一个通用 helper 进程。
     *
     * @param request 启动参数（helper 路径/argv/env/raw memfd 容量）。
     * @return 不透明 handle + PID，或具名失败。
     */
    fun spawn(request: VictimSpawnRequest): VictimResult<VictimSpawned>

    /**
     * 写入一个完整控制线帧（阻塞到写完或超时）。
     *
     * @param handle 有效 handle。
     * @param frame 完整线帧（含 4 字节长度前缀）。
     * @param timeoutMs 写超时（毫秒；>0）。
     * @return Ok 或具名失败（TIMEOUT/EOF/SEND_FAILED）。
     */
    fun send(handle: Long, frame: ByteArray, timeoutMs: Long): VictimResult<Unit>

    /**
     * 读取一个完整控制线帧（阻塞到读完或超时）。
     *
     * @param handle 有效 handle。
     * @param capacity 输出缓冲上限（≥ 线帧最大长度）。
     * @param timeoutMs 读超时（毫秒；>0）。
     * @return 完整线帧字节，或具名失败（TIMEOUT/EOF/BAD_FRAME）。
     */
    fun receive(handle: Long, capacity: Int, timeoutMs: Long): VictimResult<ByteArray>

    /**
     * 从 raw memfd 读取一段字节（不解析）。
     *
     * @param handle 有效 handle。
     * @param offset memfd 内偏移（≥0）。
     * @param length 读取长度（>0）。
     * @return 读回字节，或具名失败。
     */
    fun readRaw(handle: Long, offset: Long, length: Long): VictimResult<ByteArray>

    /**
     * @param handle 有效 handle。
     * @return raw memfd 总容量（字节），或具名失败。
     */
    fun rawSize(handle: Long): VictimResult<Long>

    /**
     * 有界等待 helper 退出并回收（waitpid/reap）。
     *
     * @param handle 有效 handle。
     * @param timeoutMs 等待上限（毫秒；>0）。
     * @return 退出码，或具名失败（TIMEOUT=未退出；WAIT_FAILED）。
     * @note 超时**不**隐式 kill；调用方决定 [kill]。
     */
    fun waitExit(handle: Long, timeoutMs: Long): VictimResult<Int>

    /**
     * 杀掉 helper 并回收（SIGKILL + waitpid）。
     *
     * @param handle 有效 handle。
     * @return 退出码或具名失败。
     */
    fun kill(handle: Long): VictimResult<Int>

    /**
     * 只 detach 父侧传输资源（关闭 control/raw/释放 handle），**不 kill、不 wait**。
     *
     * @param handle 有效 handle。
     * @note 仅用于 rooted child 驻留（不杀）；token 被消费，不得再用。
     */
    fun detach(handle: Long)

    /**
     * 关闭 handle：best-effort kill+reap+关闭父侧 fd 并消费 token（幂等由会话保证）。
     *
     * @param handle 有效 handle。
     */
    fun close(handle: Long)
}

/**
 * helper 启动参数（backend-agnostic）。
 *
 * @property helperPath `nativeLibraryDir` 内 helper PIE 绝对路径。
 * @property argv helper 自身 argv（含 argv[0]；由使用方/调试配置给出）。
 * @property env helper 环境变量（`KEY=VALUE`；空=继承调用方环境）。
 * @property rawSizeBytes raw memfd 容量（字节；≥ [VictimProtocol.RAW_HEADER_BYTES]）。
 */
internal data class VictimSpawnRequest(
    val helperPath: String,
    val argv: List<String>,
    val env: List<String>,
    val rawSizeBytes: Long,
)

/**
 * 已启动的 helper 句柄。
 *
 * @property handle Native 不透明 token（非 0）。
 * @property pid helper PID（>0；用于身份/starttime 复查）。
 */
internal data class VictimSpawned(val handle: Long, val pid: Int)
