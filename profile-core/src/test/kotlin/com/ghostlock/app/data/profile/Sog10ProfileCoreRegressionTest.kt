package com.ghostlock.app.data.profile

import com.ghostlock.app.data.AvailablePriority
import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.ProfileLayout
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.getValueAt
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class Sog10ProfileCoreRegressionTest {
    private val release = "5.15.189-android13-8-00004-g1c3825f8ac0a-ab14110541"

    @Test
    fun `SOG10 profile keeps explicit nulls and round trips through GLKv3`() {
        val profiles = File(repoRoot(), "app/src/main/assets/profile")
        val builtin = parse(File(profiles, "$release.conf"))
        assertTrue(builtin.containsKey("kernel_phys_load"))
        assertNull(builtin["kernel_phys_load"])
        val cred = requireNotNull(builtin["cred"].asValueMap())
        assertTrue(cred.containsKey("usage_offset"))
        assertNull(cred["usage_offset"])
        assertEquals(47529984L, (builtin["offset"].asValueMap()!!["empty_zero_page"] as Number).toLong())
        assertEquals(setOf("multicast_waiter"), builtin["route"].asValueMap()!!.keys)
        assertNull(builtin["fallback"])

        val tuning = parse(File(profiles, "execution-tuning.conf"))["execution"].asValueMap()
        val route = "multicast_waiter"

        val presets = RouteKind.entries.mapNotNull { kind ->
            val file = File(profiles, "execution-${kind.token.replace('_', '-')}.conf")
            if (!file.isFile) return@mapNotNull null
            val preset = parse(file)
            kind.token to requireNotNull(
                preset["execution"].asValueMap()?.get("routes").asValueMap()
                    ?.get(kind.token).asValueMap(),
            )
        }.toMap()

        fun resolve(profile: ValueMap) = ProfileMerger.resolveMerged(
            deviceRelease = release,
            builtin = profile,
            imported = null,
            overrides = null,
            tuningExecution = tuning,
            pair = CpuPairView(0, 1),
            routePresets = presets,
        )
        val merged = resolve(builtin)
        assertEquals(emptyList<ConfigError>(), ProfileResolver.validateMerged(merged, route))

        fun document(profile: ValueMap) = NativeProfileDocument.from(
            release = release,
            route = route,
            /* M4(a) as in ProfileExporter: the declared step queue is an array of
             * maps, and the three typed accessors cannot return an array. */
            raw = { path -> profile.getValueAt(path) },
            value = { path -> ProfileResolver.nativeValue(profile, route, path) },
            text = { path -> ProfileResolver.nativeText(profile, path) },
            bool = { path -> ProfileResolver.nativeBool(profile, path) },
        )
        val bytes = Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(document(merged)))
        // Explicit null is a completeness marker, not a guessed zero/address.
        val previouslySparse = parse(File(profiles, "$release.conf"))
        previouslySparse.remove("kernel_phys_load")
        previouslySparse["cred"].asValueMap()!!.remove("usage_offset")
        assertArrayEquals(
            bytes,
            Glkv3Encoder.encode(
                NativeProfileGlkv3Adapter.adapt(document(resolve(previouslySparse))),
            ),
        )
        // GLKv3 has no container header: the root is a MessagePack map marker.
        assertTrue(
            (bytes[0].toInt() and 0xff and 0xf0) == 0x80 ||
                bytes[0] == 0xDE.toByte() ||
                bytes[0] == 0xDF.toByte(),
        )

        val decoded = requireNotNull(Glkv3Decoder.decode(bytes))
        assertEquals(release, decoded.release)
        /* 沿革: 排序权威 = AvailablePriority（priority，其次 token 序）；此前用
         * BackendKind 枚举序 ⇒ 43499 排在前面 ⇒ 声明默认被绕过 ⇒ A301SO 现象。
         * 本次改为【按声明派生】期望：默认项 = orderedBackends(available) 首项，
         * 期望 route = 该默认项在目录里的 route（43284 无 route 轴 ⇒ 期望 null）。
         * 零硬编码：既不写 multicast_waiter，也不裸写 null。 */
        /* 沿革: 排序权威 = AvailablePriority（priority，其次 token 序）；此前 BackendKind
         * 枚举序把 43499 排前 ⇒ 声明被绕过 ⇒ A301SO 现象 ⇒ 本次改为【按声明派生】期望。
         * 约束: builtin 来自本地 parse(...)，它已规范化 ⇒ 按设计不含 available（运行期投影），
         * 因此从【同一份资产文本】用同一条解析入口（HoconSupport.parseValue）取原始 available，
         * 只是不做 canonicalize —— 这不是第二条读取路径。零硬编码：不写 multicast_waiter。 */
        val assetText = File(File(repoRoot(), "app/src/main/assets/profile"), "$release.conf").readText()
        val declaredRaw = requireNotNull(HoconSupport.parseValue(assetText).asValueMap())
        /* 资产的根形态是 ghostlock { ... }；兼容两种根形态（自诊断沿用）。 */
        val declaredRoot = declaredRaw["ghostlock"].asValueMap() ?: declaredRaw
        val declaredAvailable = requireNotNull(declaredRoot["available"].asValueMap()) {
            "no available declaration in the asset; root keys=" +
                declaredRoot.keys.sorted().joinToString(",")
        }
        val declaredBackends = AvailablePriority.orderedBackends(declaredAvailable)
        require(declaredBackends.isNotEmpty()) {
            "available declares nothing; keys=" + declaredAvailable.keys.sorted().joinToString(",")
        }
        val defaultBackend = declaredBackends.first()
        val defaultRoutes = CombinationCatalog.specs
            .filter { spec -> spec.backend.token == defaultBackend }
            .mapNotNull { spec -> spec.route?.token }
            .distinct()
        /* 无歧义守卫：多于一条 route ⇒ 无法派生期望 ⇒ 明确红（否则会静默变成 null = vacuous）。 */
        require(defaultRoutes.size <= 1) {
            "ambiguous default backend " + defaultBackend + " declares " + defaultRoutes +
                "; cannot derive a route expectation"
        }
        val expectedRoute = defaultRoutes.singleOrNull()
        assertEquals("the document carries the DEFAULT declaration route", expectedRoute, decoded.route)
        /* HOCON refactor: no common owner; the kernel scalars are root values. */
        assertEquals(5uL, decoded.kernelMajor)
        /* M3/M5: the queue replaced the token - the migrated profile must carry the
         * queue and NO token (native rejects both at once, glkv3_parse.cpp:407-419). */
        assertTrue(
            "the migrated profile must not carry a token",
            runCatching { entry(decoded, "backend.cve_2026_43499", "steps") }.isFailure,
        )
        /* 沿革: 排序权威 = AvailablePriority（priority，其次 token 序）；默认项 = 声明首项。
         * 队列属于【默认 backend】，不再写死 43499（BackendKind 枚举序曾把 43499 排前）。 */
        assertTrue(
            "the migrated profile must carry the default backend's queue",
            runCatching { entry(decoded, "backend." + defaultBackend, "queue") }.isSuccess,
        )
        /* 这 12 条 43499 测量值断言（8 条 abi/cred + 4 条 route）描述的是【显式选中 43499】
         * 时的文档形态；默认项由声明派生后默认文档是 43284 形态，故按乙移到显式路径测试
         * Sog10ExplicitBackendOverrideTest（覆盖条数 12 -> 12，不降级）。 */
        assertTrue(bytes.isNotEmpty())
    }

    private fun entry(document: Glkv3Document, section: String, key: String): Glkv3Value =
        document.sections
            .first { it.name == section }
            .entries
            .first { it.key == key }
            .value

    private fun entryOrNull(
        document: Glkv3Document,
        section: String,
        key: String,
    ): Glkv3Value? = document.sections
        .firstOrNull { it.name == section }
        ?.entries
        ?.firstOrNull { it.key == key }
        ?.value

    private fun parse(file: File): ValueMap {
        val parsed = requireNotNull(
            HoconSupport.parseValue(file.readText()).asValueMap(),
        ) { "cannot parse ${file.path}" }
        ProfileLayout.applyNormalize(parsed)
        return parsed
    }

    private fun repoRoot(): File {
        var current = File(System.getProperty("user.dir")).canonicalFile
        repeat(5) {
            if (File(current, "app/src/main/assets/profile").isDirectory) return current
            current = current.parentFile ?: error("cannot locate repository root")
        }
        error("cannot locate repository root")
    }
}
