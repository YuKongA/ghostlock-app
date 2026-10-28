package com.ghostlock.app.data.cve98260

import android.content.Context
import com.ghostlock.app.backend.cve98260.profile.Cve98260Profile
import com.ghostlock.app.backend.cve98260.profile.Cve98260ProfileException
import com.ghostlock.app.backend.cve98260.profile.Cve98260ProfileLoadException
import com.typesafe.config.Config
import com.typesafe.config.ConfigException
import com.typesafe.config.ConfigFactory

/**
 * CVE-2026-98260 参数 profile 加载器（B2）。
 *
 * 数据源：debug source set 的 `app/src/debug/assets/profile/98260/debug-plz110.conf`
 * （**debug-only**；release 无资产 ⇒ 后端 NotReady，技术债 TD-2）。
 * **唯一权威**是 C 头 `gl98260/params.h::struct glk98260_params`；本 loader 只做
 * fail-closed 解析与边界校验（缺键/类型不符/release 缺失/越界 ⇒ 具名拒绝，不填默认值）。
 *
 * @note 阶段：B2；未过真机门禁；profile 数值为上游推导的 **unverified 候选**。
 */
internal object Cve98260ProfileConfig {
    /** assets 路径（不带 `assets/` 前缀）。 */
    internal const val ASSET_PATH: String = "profile/98260/debug-plz110.conf"

    /** 目标设备标识（debug 资产选择；与 release 双重校验）。 */
    internal const val DEVICE: String = "PLZ110"

    /**
     * 从 app assets 读取并解析（release 不打包 ⇒ 抛具名异常）。
     *
     * @param context 应用上下文（仅打开 assets）。
     * @return 已校验的 profile。
     * @throws Cve98260ProfileLoadException 缺资产/IO 失败。
     * @throws Cve98260ProfileException 解析/校验失败。
     */
    internal fun load(context: Context): Cve98260Profile {
        val text = try {
            context.assets.open(ASSET_PATH).bufferedReader().use { it.readText() }
        } catch (problem: Throwable) {
            throw Cve98260ProfileLoadException("cve98260-profile:missing-asset", problem)
        }
        return parse(text)
    }

    /**
     * 解析 profile 文本（纯函数；宿主测试可直接调用）。
     *
     * @param text HOCON 文本。
     * @return 已校验 profile。
     * @throws Cve98260ProfileException 缺键/类型/越界。
     */
    internal fun parse(text: String): Cve98260Profile {
        val config = try {
            ConfigFactory.parseString(text).resolve()
        } catch (problem: ConfigException) {
            throw Cve98260ProfileException("cve98260-profile:invalid-hocon")
        }
        val profile = Cve98260Profile(
            release = string(config, "release"),
            kernelMajor = int(config, "kernel_major"),
            numeric = numericParams(config),
            managerPackage = string(config, "ksu.manager_package"),
            kmi = string(config, "ksu.kmi"),
            lateLoadArgs = stringList(config, "ksu.late_load_args"),
            ksudFallbacks = stringList(config, "ksu.ksud_fallbacks"),
        )
        validate(profile)
        return profile
    }

    /**
     * 边界校验（镜像 C `glk98260_params_validate`；失败即具名拒绝）。
     *
     * @param profile 已解析 profile。
     * @throws Cve98260ProfileException 任一规则不满足。
     */
    internal fun validate(profile: Cve98260Profile) {
        val n = profile.numeric
        fun fail(reason: String): Nothing = throw Cve98260ProfileException(reason)
        fun check(condition: Boolean, reason: String) {
            if (!condition) fail(reason)
        }
        val pageSize = n[1]
        val cpu = n[2]
        val sockbuf = n[3]
        val slabOrder = n[4]
        val slideMax = n[5]
        val baseMin = n[6]
        val baseMax = n[7]
        val site = n[8]
        val low21 = n[9]
        check(n[0] == 1L, "cve98260-profile:schema")
        check(pageSize == 4096L, "cve98260-profile:page-size")
        check(cpu in 0L..63L, "cve98260-profile:cpu")
        check(sockbuf > 0L && sockbuf <= (1L shl 20) && (sockbuf and (sockbuf - 1L)) == 0L,
            "cve98260-profile:sockbuf")
        check(slabOrder in 1L..6L && (pageSize shl slabOrder.toInt()) == sockbuf,
            "cve98260-profile:slab-order")
        check(slideMax in 0x200000L..0x40000000L && (slideMax and 0x1fffffL) == 0L,
            "cve98260-profile:slide-max")
        check(java.lang.Long.compareUnsigned(baseMin, 0xffffff0000000000UL.toLong()) >= 0 &&
            java.lang.Long.compareUnsigned(baseMax, 0xffffffc000000000UL.toLong()) <= 0 &&
            java.lang.Long.compareUnsigned(baseMin, baseMax) < 0,
            "cve98260-profile:base-window")
        check(isKernelPointer(site) && (site and 0x1fffffL) == low21,
            "cve98260-profile:timer-site")
        val timerAlloc = n[10]
        val credReq = n[11]
        val credAlloc = n[12]
        check(timerAlloc in 1L..0x1000L, "cve98260-profile:timer-alloc")
        check(credReq > 0L && credAlloc >= credReq && credAlloc <= 0x1000L,
            "cve98260-profile:cred-alloc")
        check(n[13] in 0L..0x100L && n[14] in 0L..0x2000L && n[24] in 0L..0x100L,
            "cve98260-profile:offsets")
        check(n[15] in 0L..0x1fffffL && n[16] in 1L..0x4000L, "cve98260-profile:task")
        check(n[17] > 0L && n[25] in 1L..0xffffffffL, "cve98260-profile:caps")
        val symbols = listOf(n[18], n[19], n[20], n[21], n[22], n[23])
        check(symbols.all(::isKernelPointer), "cve98260-profile:symbols")
    }

    /** @return true=内核地址形态（top 16 位全 1）。 */
    private fun isKernelPointer(value: Long): Boolean =
        java.lang.Long.compareUnsigned(value, 0xffff000000000000UL.toLong()) >= 0

    /** 按 C 结构体顺序收集 26 个数值参数。 */
    private fun numericParams(config: Config): List<Long> = listOf(
        ulong(config, "static.schema_version"),
        ulong(config, "page_size"),
        ulong(config, "cpu"),
        ulong(config, "sockbuf"),
        ulong(config, "slab_order"),
        ulong(config, "slide_max"),
        ulong(config, "base_min"),
        ulong(config, "base_max"),
        ulong(config, "static.timer_site"),
        ulong(config, "static.timer_site_low21"),
        ulong(config, "sizes.timer_alloc"),
        ulong(config, "sizes.cred_req"),
        ulong(config, "sizes.cred_alloc"),
        ulong(config, "offsets.cred_uid"),
        ulong(config, "offsets.task_cred"),
        ulong(config, "static.copy_process_low21"),
        ulong(config, "sizes.task_alloc_bytes"),
        ulong(config, "caps.fullcap"),
        ulong(config, "symbols.selinux_state"),
        ulong(config, "symbols.panic_on_warn"),
        ulong(config, "symbols.root_user"),
        ulong(config, "symbols.init_user_ns"),
        ulong(config, "symbols.init_ucounts"),
        ulong(config, "symbols.init_groups"),
        ulong(config, "offsets.selinux_blob"),
        ulong(config, "caps.selinux_sid_kernel"),
    )

    /** 读字符串（非空，≤256 B）。 */
    private fun string(config: Config, path: String): String {
        val value = try {
            config.getString(path).trim()
        } catch (problem: ConfigException) {
            throw Cve98260ProfileException("cve98260-profile:missing-key:" + path)
        }
        if (value.isEmpty() || value.length > 256) {
            throw Cve98260ProfileException("cve98260-profile:string:" + path)
        }
        return value
    }

    /** 读字符串列表（≤8 项、每项 1..128 B）。 */
    private fun stringList(config: Config, path: String): List<String> {
        val values = try {
            config.getStringList(path).map { it.trim() }
        } catch (problem: ConfigException) {
            throw Cve98260ProfileException("cve98260-profile:missing-key:" + path)
        }
        if (values.size > 8 || values.any { it.isEmpty() || it.length > 128 }) {
            throw Cve98260ProfileException("cve98260-profile:list:" + path)
        }
        return values
    }

    /** 读非负整数（NUMBER 或十六进制 STRING；越界/负数 ⇒ 具名拒绝）。 */
    private fun int(config: Config, path: String): Int {
        val value = try {
            config.getInt(path)
        } catch (problem: ConfigException) {
            throw Cve98260ProfileException("cve98260-profile:missing-key:" + path)
        }
        return value
    }

    /** 读 u64 位模式（NUMBER 或 `0x` STRING）。 */
    private fun ulong(config: Config, path: String): Long {
        val value = try {
            config.getValue(path)
        } catch (problem: ConfigException) {
            throw Cve98260ProfileException("cve98260-profile:missing-key:" + path)
        }
        return when (value.valueType()) {
            com.typesafe.config.ConfigValueType.NUMBER -> {
                val longValue = config.getLong(path)
                if (longValue < 0L) throw Cve98260ProfileException("cve98260-profile:negative:" + path)
                longValue
            }
            com.typesafe.config.ConfigValueType.STRING -> {
                val text = config.getString(path).trim()
                if (!text.startsWith("0x") && !text.startsWith("0X")) {
                    throw Cve98260ProfileException("cve98260-profile:not-hex:" + path)
                }
                runCatching { java.lang.Long.parseUnsignedLong(text.substring(2), 16) }
                    .getOrElse {
                        throw Cve98260ProfileException("cve98260-profile:bad-hex:" + path)
                    }
            }
            else -> throw Cve98260ProfileException("cve98260-profile:type:" + path)
        }
    }
}
