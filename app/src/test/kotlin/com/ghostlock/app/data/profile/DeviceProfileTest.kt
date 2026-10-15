package com.ghostlock.app.data.profile

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * 画像这一层：HOCON 读到什么、按 release 选到哪一档、以及**本机那份真档**里的关键值。
 *
 * 选档与读档是内核偏移进入流程的唯一入口，所以这里的判据分三类：解析正确、选档正确、
 * 以及"读不到/写错语法时必须具名失败，不能猜"。
 */
class DeviceProfileTest {
    /** 仓库里真实存在的这台设备的画像（A301SO 的内核 release）。 */
    private val deviceRelease = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557"
    private val assetDir = File("src/main/assets/profile")

    private fun assetSource(): AssetSource = AssetSource { name ->
        val file = File(assetDir, name)
        if (file.isFile) file.readText() else null
    }

    private fun assetLister(): AssetLister =
        AssetLister { assetDir.list()?.toList().orEmpty() }

    @Test
    fun theReadersOwnRules() {
        val config = Hocon.parse(
            """
            # 注释
            a {
              b = 7
              c = "text"   // 行尾注释
              d = [ { step = "w1" }, { step = "w2" } ]
              e = null
              f = true
            }
            """.trimIndent(),
        )
        assertEquals(7L, config.long("a.b"))
        assertEquals("text", config.string("a.c"))
        assertEquals(2, config.arr("a.d")?.items?.size)
        assertTrue(config.has("a.e"))
        assertEquals(true, config.bool("a.f"))
        assertNull(config.long("a.missing"))
    }

    @Test
    fun syntaxWeDoNotSupportFailsByName() {
        var failed = false
        try {
            Hocon.parse("a = 1k")
        } catch (expected: HoconException) {
            failed = true
        }
        assertTrue("不认识的写法必须具名报错", failed)

        var unterminated = false
        try {
            Hocon.parse("a { b = 1")
        } catch (expected: HoconException) {
            unterminated = true
        }
        assertTrue("未闭合的块必须报错", unterminated)
    }

    @Test
    fun theIncludeIsMergedAtItsKeyPosition() {
        val source = AssetSource { name ->
            if (name == "extra.conf") "extra_key = 5" else null
        }
        val config = Hocon.parse("top { include \"extra.conf\" own = 1 }", include = { name -> source.text(name) })
        assertEquals(5L, config.long("top.extra_key"))
        assertEquals(1L, config.long("top.own"))
    }

    @Test
    fun theAssetIsChosenByRelease() {
        val names = listOf("5.15-android13-general.conf", "5.15.189-android13-8-00016-x.conf")
        assertEquals(
            "5.15.189-android13-8-00016-x.conf" to false,
            DeviceProfile.chooseAsset("5.15.189-android13-8-00016-x", names),
        )
        assertEquals(
            "5.15-android13-general.conf" to true,
            DeviceProfile.chooseAsset("5.15.189-android13-8-00076-unknown", names),
        )
        assertNull(DeviceProfile.chooseAsset("4.19-android9-x", names))
    }

    @Test
    fun thisMachinesProfileLoadsAndDeclaresWhatWeExpect() {
        val profile = DeviceProfile.load(deviceRelease, assetSource(), assetLister()).getOrThrow()
        assertEquals(deviceRelease + ".conf", profile.assetName)
        assertFalse("这台机器有精确档，不该退到通用档", profile.generic)
        assertEquals(3L, profile.schemaVersion())
        assertTrue(profile.declares("cve_2026_43499"))
        assertTrue(profile.declares("cve_2026_43284"))
        assertEquals(listOf("w1", "w2", "w3"), profile.declaredStepNames("cve_2026_43499"))
        assertEquals(listOf("pagecache_write"), profile.declaredStepNames("cve_2026_43284"))
        assertEquals("multicast_waiter", profile.route("cve_2026_43499"))
        assertNotNull(profile.imageOffset("cve_2026_43499", "selinux_enforcing"))
    }

    @Test
    fun aMissingProfileIsANamedRefusal() {
        val empty = AssetSource { null }
        val none = AssetLister { emptyList() }
        val result = DeviceProfile.load(deviceRelease, empty, none)
        assertTrue(result.isFailure)
        assertTrue(result.exceptionOrNull()?.message?.startsWith("profile-not-found:") == true)
    }

    @Test
    fun aWrongSchemaIsRefusedNotGuessed() {
        val old = AssetSource { "ghostlock { schema_version = 2 }" }
        val lister = AssetLister { listOf(deviceRelease + ".conf") }
        val result = DeviceProfile.load(deviceRelease, old, lister)
        assertTrue(result.isFailure)
        assertEquals("profile-schema:2", result.exceptionOrNull()?.message)
    }
}
