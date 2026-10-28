package com.ghostlock.app.ui.screen.module.backends

import com.ghostlock.app.R
import com.ghostlock.app.backend.BackendId
import com.ghostlock.app.data.cve98260.Cve98260ReadinessProbe
import com.ghostlock.app.ui.backend.BackendReadiness
import com.ghostlock.app.ui.backend.BackendFlowRunner
import com.ghostlock.app.ui.backend.BackendRunCapability
import com.ghostlock.app.ui.backend.BackendUidRequirement
import com.ghostlock.app.ui.backend.BackendParameterSource
import com.ghostlock.app.ui.backend.BackendParameterScope
import com.ghostlock.app.ui.backend.BackendParameterSchema
import com.ghostlock.app.ui.backend.BackendParameterKind
import com.ghostlock.app.ui.backend.BackendParameterDecl
import com.ghostlock.app.ui.backend.ReadinessReason

/**
 *  CVE-2026-98260 的配置页/就绪声明（B3）。
 *
 *  执行通道：Shizuku UserService（shell 域进程，app 包代码）+ JNI 控制独立 payload；
 *  终局=fake cred → 内置 KernelSU late-load；**不可中止**（触发后只能重启清理）。
 *  readiness 只读后台探针快照（fail-closed：未探测/未知=NotReady），不写死 Ready。
 *
 *  @note 真机门禁 G0–G13 pending；未过真机不得标 supported/可用。
 */
object BackendCve202698260 : BackendSpec {
    override val id: BackendId = BackendId.CVE_98260

    /** 执行体 uid 需求：shell uid 2000（Shizuku 攻击线程）。 */
    override val uidRequirement: BackendUidRequirement = BackendUidRequirement.SHELL_UID_2000

    /**
     * 参数/偏移声明（值由 98260 profile 提供；这里只声明元数据）。
     *
     * @note `COMPILE_CONSTANT` 项是当前仍硬编码在 payload 的 fake-cred 布局偏移，
     *   登记为待提升参数（required=false），便于 UI/评审追踪未参数化项。
     */
    override val parameterSchema: BackendParameterSchema
        get() = parameterSchemaOf()

    override val runner: BackendFlowRunner? = com.ghostlock.app.ui.backend.Cve98260FlowRunner

    override val cve: String = "CVE-2026-98260"

    override val titleRes: Int = R.string.backend_cve_2026_98260

    /** 触发后不允许中止（不得 kill/timeout payload）；RunPage 读取本字段。 */
    override val abortable: Boolean = false


    override fun rows(): List<BackendRow> {
        val identity = Cve98260ReadinessProbe.snapshot().payloadIdentity ?: "未探测"
        return listOf(
            BackendRow(R.string.backend_row_id, cve),
            BackendRow(R.string.backend_row_requires, "Shizuku（shell uid 2000 / u:r:shell:s0）"),
            BackendRow(R.string.backend_row_channel, "UserService + JNI → 独立 payload"),
            BackendRow(R.string.backend_row_payload, identity),
            BackendRow(R.string.backend_row_status, "未过真机门禁（G0–G13 pending）"),
        )
    }

    /**
     * readiness 声明（只含输入事实：profile/payload/store/boot_id）。
     *
     * @note 执行体（Shizuku/uid 2000）不在此自检；由 UI 按 [uidRequirement] 在启动前拒绝。
     */
    override fun readiness(): BackendReadiness =
        declare(Cve98260ReadinessProbe.snapshot().reasons, Cve98260ReadinessProbe.snapshot().ready)

    /**
     * 纯函数形式（供宿主单测；不触 binder/IO）。
     *
     * @param snapshotReasons 探针给出的具名原因（`cve98260:*`）。
     * @param ready 探针是否判定 Ready。
     * @return 带「需要 Shizuku」声明的 readiness。
     */
    internal fun declare(snapshotReasons: List<String>, ready: Boolean): BackendReadiness {
        if (ready) return BackendReadiness.Ready()
        val reasons = snapshotReasons.map { reason -> ReadinessReason(reasonRes(reason), reason) }
        return BackendReadiness.NotReady(reasons)
    }

    override fun statusTextRes(): Int =
        if (readiness().isReady) {
            R.string.backend_readiness_ready
        } else {
            R.string.backend_status_cve98260_not_ready
        }

    /** 把预检 reason（`cve98260:preflight:*`）映射到本地化短句；未知回退「未声明」。 */
    private fun reasonRes(reason: String): Int = when {
        reason.contains("profile-missing") -> R.string.backend_readiness_cve98260_profile_missing
        reason.contains("release-mismatch") -> R.string.backend_readiness_cve98260_profile_release
        reason.contains("payload-missing") -> R.string.backend_readiness_cve98260_payload
        reason.contains("must-reboot") -> R.string.backend_readiness_cve98260_must_reboot
        reason.contains("boot-id") -> R.string.backend_readiness_cve98260_boot_id
        reason.contains("not-run") || reason.contains("not-attached") ->
            R.string.backend_readiness_cve98260_not_probed
        else -> R.string.backend_readiness_undeclared
    }

    /** @return 98260 参数/偏移声明（profile 项 + 待提升的硬编码 fake-cred 布局）。 */
    private fun parameterSchemaOf(): BackendParameterSchema {
        fun p(
            name: String,
            kind: BackendParameterKind,
            scope: BackendParameterScope,
            description: String,
        ): BackendParameterDecl = BackendParameterDecl(
            name = name,
            kind = kind,
            scope = scope,
            source = BackendParameterSource.PROFILE,
            description = description,
        )
        fun planned(
            name: String,
            kind: BackendParameterKind,
            scope: BackendParameterScope,
            description: String,
        ): BackendParameterDecl = BackendParameterDecl(
            name = name,
            kind = kind,
            scope = scope,
            source = BackendParameterSource.COMPILE_CONSTANT,
            required = false,
            description = description,
        )
        return BackendParameterSchema(
            listOf(
                p("schema_version", BackendParameterKind.U64, BackendParameterScope.BACKEND_PROFILE, "参数 schema 版本"),
                p("page_size", BackendParameterKind.SIZE, BackendParameterScope.BACKEND_PROFILE, "页大小（本批仅 4096）"),
                p("cpu", BackendParameterKind.CPU, BackendParameterScope.BACKEND_TUNING, "触发/exec/killer 绑核"),
                p("sockbuf", BackendParameterKind.SIZE, BackendParameterScope.BACKEND_PROFILE, "AF_UNIX 回填单次写（=order-1 slab 页）"),
                p("slab_order", BackendParameterKind.U64, BackendParameterScope.BACKEND_PROFILE, "posix_timers_cache slab order"),
                p("slide_max", BackendParameterKind.SIZE, BackendParameterScope.BACKEND_PROFILE, "KASLR slide 上界"),
                p("base_min", BackendParameterKind.ADDRESS, BackendParameterScope.BACKEND_PROFILE, "线性映射基址窗口下界"),
                p("base_max", BackendParameterKind.ADDRESS, BackendParameterScope.BACKEND_PROFILE, "线性映射基址窗口上界"),
                p("static_timer_site", BackendParameterKind.ADDRESS, BackendParameterScope.BACKEND_PROFILE, "timer 站点静态地址"),
                p("timer_site_low21", BackendParameterKind.MASK, BackendParameterScope.BACKEND_PROFILE, "timer 站点低 21 位"),
                p("timer_alloc", BackendParameterKind.SIZE, BackendParameterScope.BACKEND_PROFILE, "k_itimer 分配尺寸"),
                p("cred_req", BackendParameterKind.SIZE, BackendParameterScope.COMMON_KERNEL_ABI, "cred 分配请求尺寸"),
                p("cred_alloc", BackendParameterKind.SIZE, BackendParameterScope.COMMON_KERNEL_ABI, "cred_jar 分配尺寸"),
                p("cred_uid_off", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "cred uid/gid 偏移"),
                p("off_task_cred", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "task_struct.cred 偏移"),
                p("copy_process_low21", BackendParameterKind.MASK, BackendParameterScope.BACKEND_PROFILE, "task_struct 分配站点低 21 位"),
                p("task_alloc_bytes", BackendParameterKind.SIZE, BackendParameterScope.BACKEND_PROFILE, "task_struct 分配尺寸"),
                p("fullcap", BackendParameterKind.MASK, BackendParameterScope.COMMON_KERNEL_ABI, "全能力掩码"),
                p("selinux_state_static", BackendParameterKind.ADDRESS, BackendParameterScope.COMMON_KERNEL_ABI, "selinux_state 静态地址"),
                p("panic_on_warn_static", BackendParameterKind.ADDRESS, BackendParameterScope.BACKEND_PROFILE, "panic_on_warn 静态地址"),
                p("root_user_static", BackendParameterKind.ADDRESS, BackendParameterScope.COMMON_KERNEL_ABI, "init root_user 静态地址"),
                p("init_user_ns_static", BackendParameterKind.ADDRESS, BackendParameterScope.COMMON_KERNEL_ABI, "init_user_ns 静态地址"),
                p("init_ucounts_static", BackendParameterKind.ADDRESS, BackendParameterScope.COMMON_KERNEL_ABI, "init_ucounts 静态地址"),
                p("init_groups_static", BackendParameterKind.ADDRESS, BackendParameterScope.COMMON_KERNEL_ABI, "init_groups 静态地址"),
                p("selinux_blob_off", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "SELinux blob 内 SID 偏移"),
                p("selinux_sid_kernel", BackendParameterKind.MASK, BackendParameterScope.COMMON_KERNEL_ABI, "SECINITSID_KERNEL"),
                p("manager_package", BackendParameterKind.STRING, BackendParameterScope.BACKEND_PROFILE, "KernelSU 管理器包名"),
                p("kmi", BackendParameterKind.STRING, BackendParameterScope.BACKEND_PROFILE, "KSU KMI"),
                p("late_load_args", BackendParameterKind.TOKEN_LIST, BackendParameterScope.BACKEND_PROFILE, "KSU late-load 参数（含 --allow-shell）"),
                p("ksud_fallbacks", BackendParameterKind.TOKEN_LIST, BackendParameterScope.BACKEND_PROFILE, "ksud 回退路径"),
                planned("fake_cred_usage_off", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "fake cred usage/refcount 偏移（当前硬编码 0x00）"),
                planned("fake_cred_caps_off", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "fake cred caps 起始偏移（当前硬编码 0x30）"),
                planned("fake_cred_security_off", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "fake cred security 指针偏移（当前硬编码 0x80）"),
                planned("fake_cred_user_off", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "fake cred user 指针偏移（当前硬编码 0x88）"),
                planned("fake_cred_user_ns_off", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "fake cred user_ns 偏移（当前硬编码 0x90）"),
                planned("fake_cred_ucounts_off", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "fake cred ucounts 偏移（当前硬编码 0x98）"),
                planned("fake_cred_group_info_off", BackendParameterKind.OFFSET, BackendParameterScope.COMMON_KERNEL_ABI, "fake cred group_info 偏移（当前硬编码 0xa0）"),
            ),
        )
    }

}
