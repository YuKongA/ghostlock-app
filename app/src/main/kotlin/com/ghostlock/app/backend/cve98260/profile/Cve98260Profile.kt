package com.ghostlock.app.backend.cve98260.profile

/**
 * CVE-2026-98260 的每构建参数值对象（B2）。
 *
 * 单一权威：`app/src/main/cpp/gl98260/params.h::struct glk98260_params`
 * （字段名+顺序+类型）。本类 [PARAM_NAMES] 是其镜像，单测解析 C 头逐字段对拍；
 * **禁止手抄第二套顺序**。数值以 Long 位模式承载（无符号内核地址可 > Long.MAX）。
 *
 * @property release 目标内核 `uname -r` 的精确值（与设备不一致 ⇒ fail-closed）。
 * @property kernelMajor 内核主版本（5/6），仅作合法性检查。
 * @property numeric 26 个数值参数的位模式（顺序见 [PARAM_NAMES]）。
 * @property managerPackage KernelSU manager 包名（late-load 用）。
 * @property kmi late-load KMI（如 android16-6.12）。
 * @property lateLoadArgs late-load argv token 列表（必须含 `--allow-shell`）。
 * @property ksudFallbacks ksud 回退路径（按序尝试）。
 */
internal data class Cve98260Profile(
    val release: String,
    val kernelMajor: Int,
    val numeric: List<Long>,
    val managerPackage: String,
    val kmi: String,
    val lateLoadArgs: List<String>,
    val ksudFallbacks: List<String>,
) {
    init {
        require(numeric.size == PARAM_NAMES.size) { "cve98260-profile-param-count:" + numeric.size }
        require(release.isNotBlank()) { "cve98260-profile-release-blank" }
        require(kernelMajor == 5 || kernelMajor == 6) { "cve98260-profile-kernel-major:" + kernelMajor }
        require(managerPackage.isNotBlank()) { "cve98260-profile-manager-blank" }
        require(kmi.isNotBlank()) { "cve98260-profile-kmi-blank" }
        require(lateLoadArgs.isNotEmpty() && lateLoadArgs.contains("--allow-shell")) {
            "cve98260-profile-allow-shell-missing"
        }
        require(lateLoadArgs.all { TOKEN_REGEX.matches(it) }) { "cve98260-profile-bad-late-arg" }
        require(ksudFallbacks.all { TOKEN_REGEX.matches(it) }) { "cve98260-profile-bad-fallback" }
    }

    /** @return 逗号分隔十六进制数值段 + `|` 四个字符串段（= C 侧 argv `--params`）。 */
    internal fun toParamArg(): String {
        val numbers = numeric.joinToString(",") { "0x" + java.lang.Long.toUnsignedString(it, 16) }
        val strings = listOf(
            managerPackage,
            kmi,
            lateLoadArgs.joinToString(":"),
            ksudFallbacks.joinToString(":"),
        ).joinToString("|")
        return numbers + "|" + strings
    }

    /** @return true=设备内核 release 与本 profile 精确匹配。 */
    internal fun matchesDevice(kernelRelease: String?): Boolean =
        kernelRelease != null && kernelRelease.trim() == release

    companion object {
        /** 数值字段名与顺序（= `params.h`；单测对拍）。 */
        internal val PARAM_NAMES: List<String> = listOf(
            "schema_version", "page_size", "cpu", "sockbuf", "slab_order", "slide_max",
            "base_min", "base_max", "static_timer_site", "timer_site_low21", "timer_alloc",
            "cred_req", "cred_alloc", "cred_uid_off", "off_task_cred",
            "copy_process_low21", "task_alloc_bytes", "fullcap", "selinux_state_static",
            "panic_on_warn_static", "root_user_static", "init_user_ns_static",
            "init_ucounts_static", "init_groups_static", "selinux_blob_off",
            "selinux_sid_kernel",
        )

        /** 字段数（= C `GLK98260_PARAM_COUNT`）。 */
        internal const val PARAM_COUNT: Int = 26

        /** 字符串 token 字符集（与 C `glk_char_ok` 的字符串侧一致）。 */
        internal val TOKEN_REGEX: Regex = Regex("[A-Za-z0-9._/:\\-]+")
    }
}
