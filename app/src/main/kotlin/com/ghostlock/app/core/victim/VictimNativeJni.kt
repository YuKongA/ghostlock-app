package com.ghostlock.app.core.victim

import com.ghostlock.app.jni.VictimCloseJniResult
import com.ghostlock.app.jni.VictimSpawnJniResult
import com.ghostlock.app.jni.VictimTransportJni

/**
 * [VictimNative] 的真实 JNI 实现（实现批 B1；backend-agnostic）。
 *
 * 与 `core/victim_protocol` 的机械 API 1:1：本对象只做 Kotlin↔JNI 的值域检查、
 * 内存编组与具名错误映射；不解析帧内容、不做业务判断、无状态。
 * token 生命周期由 [VictimSession] owner 保证（spawn 得 token；close/detach 恰一次）。
 *
 * @note 本文件不引用任何 backend/43499 符号（[VictimTransportJni] 是通用 JNI 门面）；
 *   架构测试 `VictimArchitectureTest` 继续约束 `core/victim` 无 backend 依赖。
 * @note 纯映射/校验函数是 `internal` 且不触 JNI，供宿主单测覆盖。
 * @note 阶段：实现批 B1（task-53）；不改 [VictimSession] 语义（fake 测试不变）。
 */
/**
 * Native victim transport 的机械错误码/哨兵（与 `victim_protocol.h` + `jni43499.c`
 * 的同名宏对拍；宿主单测直接比对 C 头，防跨语言漂移）。
 *
 * @note 这是传输层机械错误，不含任何 backend/目标参数。
 */
internal object VictimTransportCodes {
    /** `GLK_VICTIM_ERR_INVALID_ARGUMENT`。 */
    const val INVALID_ARGUMENT: Int = -1001

    /** `GLK_VICTIM_ERR_BAD_FRAME`。 */
    const val BAD_FRAME: Int = -1002

    /** `GLK_VICTIM_ERR_TIMEOUT`。 */
    const val TIMEOUT: Int = -1003

    /** `GLK_VICTIM_ERR_EOF`。 */
    const val EOF: Int = -1004

    /** `GLK_VICTIM_ERR_SYSTEM`。 */
    const val SYSTEM: Int = -1005

    /** JNI int 入口 NOT_ACCEPTED。 */
    const val JNI_NOT_ACCEPTED_INT: Int = -2

    /** JNI int 入口 INCOMPLETE。 */
    const val JNI_INCOMPLETE_INT: Int = Int.MIN_VALUE

    /** JNI handle 入口 NOT_ACCEPTED。 */
    const val JNI_NOT_ACCEPTED_HANDLE: Long = -2L

    /** JNI handle 入口 INCOMPLETE。 */
    const val JNI_INCOMPLETE_HANDLE: Long = -1L
}

internal object VictimNativeJni : VictimNative {


    /** helper 路径字节上限（与 jni43499.c 的 JNI 域校验一致）。 */
    private const val HELPER_PATH_MAX_BYTES: Int = 4096

    override fun spawn(request: VictimSpawnRequest): VictimResult<VictimSpawned> {
        validateSpawn(request)?.let { return VictimResult.failed(it) }
        val result = VictimSpawnJniResult()
        val handle = VictimTransportJni.spawnVictim(
            request.helperPath,
            request.argv.toTypedArray(),
            request.env.toTypedArray(),
            request.rawSizeBytes,
            result,
        )
        return when {
            handle == VictimTransportCodes.JNI_NOT_ACCEPTED_HANDLE ->
                VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
            handle == VictimTransportCodes.JNI_INCOMPLETE_HANDLE ->
                VictimResult.failed(VictimError.SPAWN_FAILED.token + ":incomplete")
            handle == 0L ->
                VictimResult.failed(spawnFailureReason(result))
            result.pid <= 0 -> {
                /* token 已建立但 pid 异常：只 detach（不 kill 未知进程）。 */
                VictimTransportJni.detachVictim(handle)
                VictimResult.failed(VictimError.SPAWN_FAILED.token + ":missing-pid")
            }
            else -> VictimResult.Ok(VictimSpawned(handle = handle, pid = result.pid))
        }
    }

    override fun send(handle: Long, frame: ByteArray, timeoutMs: Long): VictimResult<Unit> {
        if (handle == 0L || frame.size !in MIN_FRAME_BYTES..VictimProtocol.WIRE_MAX_BYTES) {
            return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        }
        val timeout = nativeTimeoutOrNull(timeoutMs)
            ?: return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        val status = VictimTransportJni.sendVictim(handle, frame, timeout)
        return if (status == frame.size) {
            VictimResult.Ok(Unit)
        } else {
            VictimResult.failed(mapError(status, VictimError.SEND_FAILED))
        }
    }

    override fun receive(
        handle: Long,
        capacity: Int,
        timeoutMs: Long,
    ): VictimResult<ByteArray> {
        if (handle == 0L || capacity < VictimProtocol.WIRE_MAX_BYTES) {
            return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        }
        val timeout = nativeTimeoutOrNull(timeoutMs)
            ?: return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        val out = ByteArray(VictimProtocol.WIRE_MAX_BYTES)
        val status = VictimTransportJni.receiveVictim(handle, out, timeout)
        return when {
            status >= MIN_FRAME_BYTES && status <= out.size ->
                VictimResult.Ok(out.copyOf(status))
            status == VictimTransportCodes.JNI_NOT_ACCEPTED_INT ->
                VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
            status == VictimTransportCodes.JNI_INCOMPLETE_INT ->
                VictimResult.failed(VictimError.BAD_FRAME.token + ":incomplete")
            else -> VictimResult.failed(mapError(status, VictimError.BAD_FRAME))
        }
    }

    override fun readRaw(
        handle: Long,
        offset: Long,
        length: Long,
    ): VictimResult<ByteArray> {
        if (handle == 0L || offset < 0L || length <= 0L || length > Int.MAX_VALUE.toLong()) {
            return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        }
        val out = ByteArray(length.toInt())
        val status = VictimTransportJni.readRawVictim(handle, offset, out, length.toInt())
        return when {
            status == length.toInt() -> VictimResult.Ok(out)
            status == VictimTransportCodes.JNI_NOT_ACCEPTED_INT ->
                VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
            status == VictimTransportCodes.JNI_INCOMPLETE_INT ->
                VictimResult.failed(VictimError.RAW_READ_FAILED.token + ":incomplete")
            else -> VictimResult.failed(mapError(status, VictimError.RAW_READ_FAILED))
        }
    }

    override fun rawSize(handle: Long): VictimResult<Long> {
        if (handle == 0L) return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        val size = VictimTransportJni.rawSizeVictim(handle)
        return when {
            size > 0L -> VictimResult.Ok(size)
            size == VictimTransportCodes.JNI_NOT_ACCEPTED_HANDLE ->
                VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
            else -> VictimResult.failed(mapError(size.toInt(), VictimError.INVALID_ARGUMENT))
        }
    }

    override fun waitExit(handle: Long, timeoutMs: Long): VictimResult<Int> {
        val timeout = nativeTimeoutOrNull(timeoutMs)
            ?: return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        if (handle == 0L) return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        val status = VictimTransportJni.waitExitVictim(handle, timeout)
        return when {
            status >= 0 -> VictimResult.Ok(status)
            status == VictimTransportCodes.JNI_NOT_ACCEPTED_INT ->
                VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
            status == VictimTransportCodes.JNI_INCOMPLETE_INT ->
                VictimResult.failed(VictimError.WAIT_FAILED.token + ":incomplete")
            else -> VictimResult.failed(mapError(status, VictimError.WAIT_FAILED))
        }
    }

    override fun kill(handle: Long): VictimResult<Int> {
        if (handle == 0L) return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        val status = VictimTransportJni.killVictim(handle)
        return when {
            status >= 0 -> VictimResult.Ok(status)
            status == VictimTransportCodes.JNI_NOT_ACCEPTED_INT ->
                VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
            status == VictimTransportCodes.JNI_INCOMPLETE_INT ->
                VictimResult.failed(VictimError.KILL_FAILED.token + ":incomplete")
            else -> VictimResult.failed(mapError(status, VictimError.KILL_FAILED))
        }
    }

    override fun detach(handle: Long) {
        if (handle == 0L) return
        VictimTransportJni.detachVictim(handle)
    }

    override fun close(handle: Long) {
        if (handle == 0L) return
        VictimTransportJni.closeVictim(handle, VictimCloseJniResult())
    }

    /**
     * 纯函数：spawn 请求的 Kotlin 域校验（与 C 侧同界；不触 JNI）。
     *
     * @param request 启动请求。
     * @return null=合法；否则具名 `victim:*` reason。
     */
    internal fun validateSpawn(request: VictimSpawnRequest): String? {
        val pathBytes = request.helperPath.toByteArray().size
        if (pathBytes <= 0 || pathBytes > HELPER_PATH_MAX_BYTES) {
            return VictimError.INVALID_ARGUMENT.token
        }
        if (request.argv.size !in 1..VictimProtocol.MAX_ARG_COUNT) {
            return VictimError.INVALID_ARGUMENT.token
        }
        if (request.argv.any { item ->
                val bytes = item.toByteArray().size
                bytes <= 0 || bytes > VictimProtocol.MAX_ARG_BYTES
            }
        ) {
            return VictimError.INVALID_ARGUMENT.token
        }
        if (request.env.size > VictimProtocol.MAX_ARG_COUNT) {
            return VictimError.INVALID_ARGUMENT.token
        }
        if (request.env.any { item ->
                val bytes = item.toByteArray().size
                bytes <= 0 || bytes > VictimProtocol.MAX_ARG_BYTES
            }
        ) {
            return VictimError.INVALID_ARGUMENT.token
        }
        if (request.rawSizeBytes < VictimProtocol.RAW_HEADER_BYTES.toLong()) {
            return VictimError.INVALID_ARGUMENT.token
        }
        return null
    }

    /**
     * 纯函数：spawn 失败回执到具名 reason。
     *
     * @param result JNI spawn 回执。
     * @return `victim:*` reason。
     */
    internal fun spawnFailureReason(result: VictimSpawnJniResult): String =
        if (result.callStatus == 1) {
            VictimError.INVALID_ARGUMENT.token
        } else {
            VictimError.SPAWN_FAILED.token
        }

    /**
     * 纯函数：Native 机械错误码 → 具名 VictimError token。
     *
     * @param code core/ JNI 返回值（负数为错误）。
     * @param fallback 该调用点的默认错误（SYSTEM 等无法细分时）。
     * @return 稳定 `victim:*` reason。
     */
    internal fun mapError(code: Int, fallback: VictimError): String = when (code) {
        VictimTransportCodes.INVALID_ARGUMENT -> VictimError.INVALID_ARGUMENT.token
        VictimTransportCodes.BAD_FRAME -> VictimError.BAD_FRAME.token
        VictimTransportCodes.TIMEOUT -> VictimError.TIMEOUT.token
        VictimTransportCodes.EOF -> VictimError.EOF.token
        VictimTransportCodes.SYSTEM -> fallback.token
        VictimTransportCodes.JNI_NOT_ACCEPTED_INT -> VictimError.INVALID_ARGUMENT.token
        VictimTransportCodes.JNI_INCOMPLETE_INT -> fallback.token + ":incomplete"
        else -> fallback.token
    }

    /**
     * @param timeoutMs Kotlin 毫秒超时。
     * @return Native int32 超时；非法（≤0/超 Int.MAX）返回 null。
     */
    private fun nativeTimeoutOrNull(timeoutMs: Long): Int? =
        if (timeoutMs > 0L && timeoutMs <= Int.MAX_VALUE.toLong()) {
            timeoutMs.toInt()
        } else {
            null
        }

    /** 控制线帧最小字节数（长度前缀 + body 固定开销）。 */
    private const val MIN_FRAME_BYTES: Int =
        VictimProtocol.LENGTH_PREFIX_BYTES + VictimProtocol.BODY_OVERHEAD_BYTES
}
