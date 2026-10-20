package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.util.Base64

/**
 * 一类收尾载荷（2026-10-19 用户裁定：同一类可以重复出现）。
 *
 * @property id 线上编号；只用于持久化与日志，不参与 UI 文案。
 */
enum class PayloadKind(val id: String) {
    Root("root"),
    Command("cmd"),
    Lkm("lkm"),
    ;

    companion object {
        /** @return 编号对应的类型；未知返回 null（旧数据/损坏数据不炸）。 */
        fun fromId(id: String): PayloadKind? = entries.firstOrNull { it.id == id }
    }
}

/**
 * 一条载荷实例：同一 [kind] 可出现多次，每条实例自带参数。
 *
 * 这样「两条不同命令」「两个不同 .ko」不需要新增模型（2026-10-19 用户裁定）。
 *
 * @property id 实例 id：列表 key，也是输入解析（脚本 / LKM 路径）的键。
 * @property kind 载荷类型。
 * @property enabled 开关：关掉的实例保留在表里但不进脚本（2026-10-19 用户裁定开关回归）。
 * @property scriptMode kind=Command 时 0=命令、1=脚本。
 * @property command kind=Command 且 mode=0 时的命令正文。
 * @property scriptUri kind=Command 且 mode=1 时选中的脚本 URI。
 * @property lkmUri kind=Lkm 时选中的 .ko URI。
 * @property lkmParams kind=Lkm 时 insmod 的附加参数。
 */
data class PayloadSlot(
    val id: String,
    val kind: PayloadKind,
    val enabled: Boolean = true,
    val scriptMode: Int = 0,
    val command: String = "",
    val scriptUri: String? = null,
    val lkmUri: String? = null,
    val lkmParams: String = "",
)

/** 首次安装时默认那一条 root 载荷的 id。 */
const val DEFAULT_ROOT_SLOT_ID = "slot-root"

/** 收尾选项：最终都收敛为「以 root 执行一段收尾脚本」。 */
data class FinishState(
    val rootManager: String = "KernelSU",
    /** 安全模式：作为 ksud late-load 的 --safe-mode 参数，禁用所有模块（2026-10-19）。 */
    val safeMode: Boolean = false,
    /** 载荷实例表：顺序即执行顺序，可重复、可删除。 */
    val payloads: List<PayloadSlot> = listOf(PayloadSlot(DEFAULT_ROOT_SLOT_ID, PayloadKind.Root)),
    val restoreSelinux: Boolean = false,
    /** 43284：加载中继 LKM 后开启内核内存访问接口（等待插件连接 5 秒）。 */
    val kernelAccessAfterRelay: Boolean = false,
    /** 收尾脚本最后重启 zygote（软重启）。 */
    val softRebootAfterFinish: Boolean = false,
)

/** 新建一条载荷；id 只要求在同一次会话内唯一。 */
fun newPayloadSlot(kind: PayloadKind): PayloadSlot =
    PayloadSlot(id = "slot-" + java.util.UUID.randomUUID(), kind = kind)

/**
 * 这条载荷是否已经填完整（2026-10-19 用户裁定：不完整要标红并阻止保存）。
 *
 * root 由全局 rootManager 决定，卡片上没有必填项，视为完整。
 *
 * @return false 表示缺参数，脚本生成时会被跳过。
 */
fun PayloadSlot.isComplete(): Boolean = when (kind) {
    PayloadKind.Root -> true
    PayloadKind.Command -> if (scriptMode == 0) command.isNotBlank() else !scriptUri.isNullOrBlank()
    PayloadKind.Lkm -> !lkmUri.isNullOrBlank()
}

/**
 *  收尾配置（2026-10-18 接线；2026-10-19 改为载荷实例表）。
 *
 *  UI 上是可增删的四类载荷；交给后端时收敛成一个 root 脚本 + 每条的输入路径：
 *   · 启动 root 管理器 → insmod <manager.ko>
 *   · 以 root 执行应用 → am start / app_process 包装（由脚本决定）
 *   · 以 root 执行自定义脚本 → sh <script>
 *   · 加载自定义 lkm → insmod <lkm>
 */
object FinishConfig {
    private const val PREFS = "ghostlock.finish"
    private const val KEY = "state"

    /** 持久化格式版本：1 是四布尔旧格式，2 是载荷实例表。 */
    private const val FORMAT_VERSION = "2"

    var state by mutableStateOf(FinishState())
        private set

    /**
     * root 管理器描述：包名列表 + 管理器自带的加载器二进制 + 参数。
     *
     * 以后加 ForkPatch 等只需新增一条 spec（不同包名/二进制/参数），
     * 脚本生成与 UI 都不需要改。
     */
    data class RootManagerSpec(
        val name: String,
        val packages: List<String>,
        val binary: String,
        val args: List<String>,
        /** true = 管理器本身不提供 ksud late-load，root 由已加载的 KernelPatch/apd 提供。 */
        val launchOnly: Boolean = false,
    )

    val rootManagers = listOf(
        RootManagerSpec("KernelSU", listOf("me.weishu.kernelsu"), "libksud.so", listOf("late-load", "--allow-shell")),
        RootManagerSpec("KernelSU Next", listOf("me.weishu.kernelsu.pr"), "libksud.so", listOf("late-load", "--allow-shell")),
        RootManagerSpec("ReSukiSU", listOf("com.resukisu.resukisu"), "libksud.so", listOf("late-load", "--allow-shell")),
        RootManagerSpec("SuperManager", listOf("com.kowx712.supermanager"), "libksud.so", listOf("late-load", "--allow-shell")),
        RootManagerSpec("FolkPatch", listOf("me.yuki.folk", "me.bmax.apatch"), "", emptyList(), launchOnly = true),
    )

    fun spec(name: String): RootManagerSpec? = rootManagers.firstOrNull { it.name == name }

    /** 管理器是否已安装（任一包名存在即算）。 */
    fun installedPackages(context: Context, spec: RootManagerSpec): List<String> =
        spec.packages.filter { pkg -> runCatching { context.packageManager.getPackageInfo(pkg, 0) }.isSuccess }

    fun load(context: Context) {
        val raw = context.getSharedPreferences(PREFS, 0).getString(KEY, null) ?: return
        state = decode(raw)
    }

    fun update(context: Context, value: FinishState) {
        state = value
        context.getSharedPreferences(PREFS, 0).edit().putString(KEY, encode(value)).apply()
    }

    /* ---------------- 持久化 ---------------- */

    /** 格式 v2：2|rootManager|selinux|access|reboot|slots；slots 用分号分条、冒号分字段、字符串字段 Base64。 */
    private fun encode(value: FinishState): String {
        val slots = value.payloads.joinToString(";") { slot ->
            listOf(
                slot.kind.id,
                /* legacy app 字段：App 载荷已删除（2026-10-19），保留空占位避免旧数据错位。 */
                b64(""),
                slot.scriptMode.toString(),
                b64(slot.command),
                b64(slot.scriptUri.orEmpty()),
                b64(slot.lkmUri.orEmpty()),
                b64(slot.lkmParams),
                flag(slot.enabled),
            ).joinToString(":")
        }
        return listOf(
            FORMAT_VERSION,
            value.rootManager,
            flag(value.restoreSelinux),
            flag(value.kernelAccessAfterRelay),
            flag(value.softRebootAfterFinish),
            slots,
            flag(value.safeMode),
        ).joinToString("|")
    }

    /** @return 解出的状态；坏字段按默认值处理，不让旧数据把页面打挂。 */
    private fun decode(raw: String): FinishState {
        val parts = raw.split("|")
        if (parts.firstOrNull() != FORMAT_VERSION) return migrateLegacy(parts)
        return FinishState(
            rootManager = parts.getOrElse(1) { "KernelSU" },
            payloads = parts.getOrElse(5) { "" }.split(";").filter { it.isNotBlank() }.mapNotNull(::decodeSlot),
            restoreSelinux = parts.getOrElse(2) { "0" } == "1",
            kernelAccessAfterRelay = parts.getOrElse(3) { "0" } == "1",
            softRebootAfterFinish = parts.getOrElse(4) { "0" } == "1",
            safeMode = parts.getOrElse(6) { "0" } == "1",
        )
    }

    private fun decodeSlot(encoded: String): PayloadSlot? {
        val fields = encoded.split(":")
        val kind = PayloadKind.fromId(fields.getOrElse(0) { "" }) ?: return null
        return PayloadSlot(
            id = newPayloadSlot(kind).id,
            kind = kind,
            scriptMode = fields.getOrElse(2) { "0" }.toIntOrNull() ?: 0,
            command = unb64(fields.getOrElse(3) { "" }),
            scriptUri = unb64(fields.getOrElse(4) { "" }).ifBlank { null },
            lkmUri = unb64(fields.getOrElse(5) { "" }).ifBlank { null },
            lkmParams = unb64(fields.getOrElse(6) { "" }),
            enabled = fields.getOrElse(7) { "1" } != "0",
        )
    }

    /** 旧格式（v1 四布尔 + order）转成载荷实例表。 */
    private fun migrateLegacy(parts: List<String>): FinishState {
        if (parts.size < 8) return FinishState()
        val order = parts.getOrElse(14) { "" }.split(',').filter { it.isNotBlank() }.ifEmpty { LEGACY_ORDER }
        val slots = order.mapNotNull { id ->
            when (id) {
                "root" -> if (parts[0] == "1") PayloadSlot("slot-root", PayloadKind.Root) else null
                "cmd" -> if (parts.getOrElse(4) { "0" } == "1") {
                    PayloadSlot(
                        "slot-cmd",
                        PayloadKind.Command,
                        scriptMode = parts.getOrElse(9) { "0" }.toIntOrNull() ?: 0,
                        command = parts.getOrElse(10) { "" },
                        scriptUri = parts.getOrElse(5) { "" }.ifBlank { null },
                    )
                } else {
                    null
                }
                "lkm" -> if (parts.getOrElse(6) { "0" } == "1") {
                    PayloadSlot(
                        "slot-lkm",
                        PayloadKind.Lkm,
                        lkmUri = parts.getOrElse(7) { "" }.ifBlank { null },
                        lkmParams = parts.getOrElse(8) { "" },
                    )
                } else {
                    null
                }
                else -> null
            }
        }
        return FinishState(
            rootManager = parts.getOrElse(1) { "KernelSU" },
            payloads = slots,
            restoreSelinux = parts.getOrElse(11) { "0" } == "1",
            kernelAccessAfterRelay = parts.getOrElse(12) { "0" } == "1",
            softRebootAfterFinish = parts.getOrElse(13) { "0" } == "1",
        )
    }

    private val LEGACY_ORDER = listOf("root", "app", "cmd", "lkm")

    private fun flag(value: Boolean): String = if (value) "1" else "0"

    private fun b64(value: String): String =
        Base64.getEncoder().encodeToString(value.toByteArray(Charsets.UTF_8))

    private fun unb64(value: String): String =
        runCatching { String(Base64.getDecoder().decode(value), Charsets.UTF_8) }.getOrDefault("")
}
