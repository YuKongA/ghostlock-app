package com.ghostlock.app.backend.backends.cve43499glued

import android.content.Context
import android.net.Uri
import com.ghostlock.app.data.AssetConfigLoader
import com.ghostlock.app.data.BuiltinProfileCatalog
import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.LegacyProfileConverter
import com.ghostlock.app.data.Profile
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.copyValue
import com.ghostlock.app.data.profile.CpuPairView
import com.ghostlock.app.data.profile.ProfileMerger
import com.ghostlock.app.data.profile.ProfileResolver
import com.ghostlock.app.data.route.RouteKind
import java.io.ByteArrayOutputStream

/** A validated upstream profile and its current GLK1 binary transport. */
internal data class Resolved43499Profile(
    val release: String,
    val sourceText: String,
    val binary: ByteArray,
)

/**
 * Resolves an upstream built-in or imported HOCON profile for the 43499 exec.
 *
 * The `kernel_profiles/index.conf` catalogue is the built-in source of truth;
 * legacy extractor documents pass through the upstream profile converter and
 * the result is validated and encoded by the current Kotlin wire model.
 */
internal object Glued43499ProfileCodec {
    private const val BuiltinPrefix = "builtin:"
    private const val ImportedPrefix = "uri:"
    private const val MaxImportedBytes = 1024 * 1024

    /** Lists the selectable built-ins indexed by the upstream asset manifest. */
    fun builtins(context: Context): List<BuiltinProfileCatalog.ProfileAsset> =
        BuiltinProfileCatalog(context).selectableProfiles

    /** Loads, validates, and encodes the selected profile; invalid input throws with a field name. */
    fun resolve(context: Context, reference: String): Resolved43499Profile {
        val sourceText = when {
            reference.startsWith(BuiltinPrefix) -> {
                val release = reference.removePrefix(BuiltinPrefix)
                val asset = builtins(context).firstOrNull { it.release == release }
                    ?: error("builtin-profile-not-indexed:$release")
                AssetConfigLoader(context).load("kernel_profiles/${asset.file}")
                    .takeIf { it.isNotBlank() }
                    ?: error("builtin-profile-empty:$release")
            }

            reference.startsWith(ImportedPrefix) -> {
                val rawUri = reference.removePrefix(ImportedPrefix)
                readImported(context, Uri.parse(rawUri))
            }

            else -> error("profile-not-selected")
        }
        return encode(sourceText, context)
    }

    /** Parses current or legacy HOCON, applies the upstream converter, then emits current GLK1 bytes. */
    fun validateImported(context: Context, uri: Uri): Resolved43499Profile =
        encode(readImported(context, uri), context)

    private fun readImported(context: Context, uri: Uri): String {
        val input = context.contentResolver.openInputStream(uri) ?: error("profile-uri-unreadable")
        input.use { stream ->
            val output = ByteArrayOutputStream()
            val chunk = ByteArray(8192)
            while (true) {
                val count = stream.read(chunk)
                if (count < 0) break
                require(output.size() + count <= MaxImportedBytes) { "profile-import-too-large" }
                output.write(chunk, 0, count)
            }
            return String(output.toByteArray(), Charsets.UTF_8)
        }
    }

    /**
     * 生产入口：解析/转换 profile 后**合并执行调优**（H1 修复），再校验/编码 GLK1。
     *
     * @param sourceText 原始 HOCON（内含或缺失 execution 均可）。
     * @param context assets 读取器（tuning/route presets）。
     * @return [Resolved43499Profile]（sourceText 原样；binary 基于 merged）。
     * @throws IllegalArgumentException tuning 缺失/空：`profile-execution-tuning-missing`。
     */
    private fun encode(sourceText: String, context: Context): Resolved43499Profile {
        val loader = AssetConfigLoader(context)
        val tuning = loadTuningExecution(loader)
        val presets = loadRoutePresets(loader)
        return buildResolved(sourceText, convertProfile(sourceText), tuning, presets)
    }

    /**
     * JVM 测试 seam：固定 tuning/presets 走同一 merge+validate+encode（不读 assets）。
     *
     * @param sourceText 原始 HOCON。
     * @param tuningExecution `.execution` map（必需；null/空 ⇒ `profile-execution-tuning-missing`）。
     * @param routePresets route→preset map（可空）。
     * @return 与生产一致的 [Resolved43499Profile]。
     */
    internal fun encodeForTest(
        sourceText: String,
        tuningExecution: ValueMap?,
        routePresets: Map<String, ValueMap>,
    ): Resolved43499Profile {
        val tuning = requireTuning(tuningExecution)
        return buildResolved(sourceText, convertProfile(sourceText), tuning, routePresets)
    }

    /**
     * JVM 测试 seam：只返回 merged ValueMap（用于断言 execution 关键值）。
     *
     * @param sourceText 原始 HOCON。
     * @param tuningExecution `.execution` map（必需）。
     * @param routePresets route→preset map。
     * @return merged profile（含 execution.*）。
     */
    internal fun mergeForTest(
        sourceText: String,
        tuningExecution: ValueMap?,
        routePresets: Map<String, ValueMap>,
    ): ValueMap = mergeProfile(convertProfile(sourceText), requireTuning(tuningExecution), routePresets)

    /** @return 转换后的 profile ValueMap（HOCON→LegacyProfileConverter）。 */
    private fun convertProfile(sourceText: String): ValueMap {
        val parsed = HoconSupport.parseValue(sourceText)?.copyValue().asValueMap()
            ?: error("profile-hocon-not-an-object")
        return LegacyProfileConverter.convertValue(parsed)
            ?: error("profile-conversion-failed")
    }

    /** @return 必需 tuning（null/空 ⇒ 具名失败，不静默 0）。 */
    private fun requireTuning(tuningExecution: ValueMap?): ValueMap {
        require(!tuningExecution.isNullOrEmpty()) { "profile-execution-tuning-missing" }
        return tuningExecution
    }

    /** @return merged profile（tuning defaults < profile 自带值；route presets 只补缺失键）。 */
    private fun mergeProfile(
        profile: ValueMap,
        tuningExecution: ValueMap,
        routePresets: Map<String, ValueMap>,
    ): ValueMap {
        val release = profile["release"] as? String ?: error("profile-release-missing")
        val pair = resolvePair(profile, tuningExecution)
        return ProfileMerger.resolveMerged(
            deviceRelease = release,
            builtin = profile,
            imported = null,
            overrides = null,
            tuningExecution = tuningExecution,
            pair = pair,
            routePresets = routePresets,
        )
    }

    /** @return 基于 merged 的校验/typed/binary 结果。 */
    private fun buildResolved(
        sourceText: String,
        profile: ValueMap,
        tuningExecution: ValueMap,
        routePresets: Map<String, ValueMap>,
    ): Resolved43499Profile {
        val merged = mergeProfile(profile, tuningExecution, routePresets)
        val routeToken = merged["route"].asValueMap()?.keys?.singleOrNull()
            ?: error("profile-route-must-select-one-route")
        val fallbackToken = merged["fallback"].asValueMap()?.get("to") as? String
        val errors = ProfileResolver.validateMerged(merged, routeToken, fallbackToken)
        require(errors.isEmpty()) {
            "profile-invalid:" + errors.joinToString(",") { it.fieldPath + ":" + it.message }
        }
        val release = merged["release"] as? String
            ?: error("profile-release-missing")
        val typed = Profile.fromValueMap(
            release = release,
            route = RouteKind.fromToken(routeToken),
            fallbackTo = RouteKind.fromToken(fallbackToken),
            value = { path -> ProfileResolver.nativeValue(merged, routeToken, fallbackToken, path) },
        ) ?: error("profile-route-unresolved")
        return Resolved43499Profile(release, sourceText, typed.toBinary())
    }

    /** @return CPU pair：profile selected_cpus > profile recommended_cpus > tuning recommended_cpus > (0,1)。 */
    private fun resolvePair(profile: ValueMap, tuningExecution: ValueMap): CpuPairView {
        val execution = profile["execution"].asValueMap()
        pairFrom(execution?.get("selected_cpus").asValueMap())?.let { return it }
        pairFrom(execution?.get("recommended_cpus").asValueMap())?.let { return it }
        pairFrom(tuningExecution["recommended_cpus"].asValueMap())?.let { return it }
        return CpuPairView(0, 1)
    }

    /** @return main/consumer 数字对；缺失返回 null。 */
    private fun pairFrom(map: ValueMap?): CpuPairView? {
        val main = (map?.get("main") as? Number)?.toInt() ?: return null
        val consumer = (map?.get("consumer") as? Number)?.toInt() ?: return null
        return CpuPairView(main, consumer)
    }

    /**
     * 读取 assets tuning：`kernel_profiles/execution-tuning.conf` 的 `.execution`。
     *
     * @return 非空 execution；缺失/空 ⇒ `profile-execution-tuning-missing`。
     */
    private fun loadTuningExecution(loader: AssetConfigLoader): ValueMap {
        val execution = parseExecution(loader.load(TUNING_ASSET))
        require(!execution.isNullOrEmpty()) { "profile-execution-tuning-missing" }
        return execution
    }

    /**
     * 读取 route presets：`kernel_profiles/execution-<route>.conf` 的 `.execution.routes.<route>`。
     *
     * @return route token → preset map；不存在的资产跳过（不新增资产）。
     */
    private fun loadRoutePresets(loader: AssetConfigLoader): Map<String, ValueMap> =
        RouteKind.entries.mapNotNull { kind ->
            val token = kind.token
            val execution = parseExecution(
                loader.load("kernel_profiles/execution-${token.replace('_', '-')}.conf"),
            ) ?: return@mapNotNull null
            val preset = execution.get("routes").asValueMap()?.get(token).asValueMap()
                ?: return@mapNotNull null
            token to preset
        }.toMap()

    /** @return 文本中的 `.execution` map；空白/不可解析返回 null。 */
    private fun parseExecution(text: String): ValueMap? {
        if (text.isBlank()) return null
        return HoconSupport.parseValue(text)?.copyValue().asValueMap()?.get("execution").asValueMap()
    }

    /** 必需 tuning 资产路径。 */
    private const val TUNING_ASSET = "kernel_profiles/execution-tuning.conf"


}
