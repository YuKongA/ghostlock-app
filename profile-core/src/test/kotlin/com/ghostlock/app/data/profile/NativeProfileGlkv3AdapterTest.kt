package com.ghostlock.app.data.profile

import com.ghostlock.app.data.CredTemplate
import com.ghostlock.app.data.Cve2026_43284Config
import com.ghostlock.app.data.ExecutionTuning
import com.ghostlock.app.data.KernelOffsetTable
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.TaskStructOffsets
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.route.MulticastConfig
import com.ghostlock.app.data.route.MulticastGeometry
import com.ghostlock.app.data.route.NoRouteConfig
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * GLKv3-3 path -> type adapter tests (S4 R2 owner-qualified).
 *
 * [golden] is the canonical hex the native glkv3::encode produces for the
 * identical logical document (built by core/tests/glkv3_schema_test.cpp); the
 * adapter output must match it byte for byte, which pins key order, integer
 * width and the Bool/Int/UInt choices to the native GLKv3 FieldSpec lists.
 */
class NativeProfileGlkv3AdapterTest {

    /* native glkv3_schema_test profile hex (MPack canonical writer). */
    private val golden =
        "86a76261636b656e64ae6376655f323032365f3433343939a772656c65617365d928352e31352e3138392d616e64726f696431332d382d30303031362d67353162626134333039616163a5726f757465b06d756c7469636173745f776169746572a6736368656d6103a873656374696f6e73de0011b66261636b656e642e6376655f323032365f343334393981a5737465707302bb6261636b656e642e6376655f323032365f34333439392e6372656488aa636170735f636f756e7400aa636170735f76616c7565cf123456789abcdef0a9636f70795f73697a6500aa726566305f696d616765cf1111111111111111aa726566315f696d61676500aa726566325f696d61676500aa726566335f696d61676500ab75736167655f76616c756500d9296261636b656e642e6376655f323032365f34333439392e657865637574696f6e2e636f6e73756d657282ab62757273745f63616c6c7300a96d61785f63616c6c7300d9286261636b656e642e6376655f323032365f34333439392e657865637574696f6e2e68616e646f666685b5656e666f7263655f706f6c6c5f617474656d70747300b8656e666f7263655f706f6c6c5f696e74657276616c5f6d7300b46d6f64756c655f706f6c6c5f617474656d70747300b76d6f64756c655f706f6c6c5f696e74657276616c5f6d7300b67072655f64697370617463685f736574746c655f6d7300d9256261636b656e642e6376655f323032365f34333439392e657865637574696f6e2e6865617083b76b65726e656c736e697463685f74696d656f75745f6d7300b4707265706172655f6d61785f617474656d70747300b2707265706172655f74696d656f75745f6d7300d9256261636b656e642e6376655f323032365f34333439392e657865637574696f6e2e7261636584b5726f7574655f646f6e655f74696d656f75745f6d7300ad726f7574655f776169745f6d7300af73657475705f736574746c655f757300b673746174655f706f6c6c5f696e74657276616c5f757300d9316261636b656e642e6376655f323032365f34333439392e657865637574696f6e2e7265636f6d6d656e6465645f6370757382a8636f6e73756d657200a46d61696e00d9276261636b656e642e6376655f323032365f34333439392e657865637574696f6e2e73746167657388ab77315f617474656d70747300ba77315f736372617463685f7265706169725f617474656d70747300ac77315f736574746c655f757300ab77325f617474656d70747300ac77325f736574746c655f757300ab77335f617474656d70747300af77335f636861696e5f726f756e647300ac77335f736574746c655f757300bd6261636b656e642e6376655f323032365f34333439392e6b65726e656c83ae636f6d706163745f776169746572c3b76b65726e656c736e697463685f636f6c6c6973696f6e7307ac6d6d5f7374727563745f737acd0400bd6261636b656e642e6376655f323032365f34333439392e6f666673657484ad736c6964655f626f6f745f696400b1736c6964655f6c6f67676572735f305f3100b3736c6964655f6e66756c6e6c5f6c6f6767657200ae76725f7379735f657869745f74702ad92d6261636b656e642e6376655f323032365f34333439392e726f7574652e6d756c7469636173745f77616974657287a861726d5f686f6c64cd4e20ac61726d5f73657175656e636504a8617474656d70747303ab6275666665725f73697a65cd0200ab6c6f636b5f6f666673657440ab7461736b5f6f666673657430aa7761697465725f6f6666fea6636f6d6d6f6e84ae66616c6c6261636b5f726f75746502ac6b65726e656c5f6d616a6f7205a9736166655f6d6f6465c3a876725f6775617264c3bc636f756e7465726d6561737572652e7669766f5f76725f677561726481b07472616365706f696e745f66756e637320b1706c6174666f726d2e6162692e6372656487ab636170735f6f666673657400ab726566305f6f666673657400ab726566315f6f666673657400ab726566325f6f666673657400ab726566335f6f666673657400a97265665f636f756e7400ac75736167655f6f666673657400b3706c6174666f726d2e6162692e6b65726e656c82b06b65726e656c5f706879735f6c6f6164cdb000b26b65726e656c5f706879735f6f6666736574cdc000b3706c6174666f726d2e6162692e6f666673657487af656d7074795f7a65726f5f7061676500a9696e69745f6372656400a9696e69745f7461736bce02112400af726f6f745f7461736b5f67726f757000b373656375726974795f686f6f6b5f686561647300b273656c696e75785f626c6f625f73697a657300b173656c696e75785f656e666f7263696e6700b8706c6174666f726d2e6162692e7461736b5f7374727563748fac61746f6d69635f666c61677300a4636f6d6d00a46372656400ab6e6f726d616c5f7072696f00ad70695f626c6f636b65645f6f6e00a770695f6c6f636b00ab70695f746f705f7461736b00aa70695f7761697465727300a370696400a47072696f65a97265616c5f6372656400b073636865645f7461736b5f67726f757000a7736563636f6d7000a57461736b7300a47467696400a87465726d696e616caa726f6f745f6368696c64"

    @Test
    fun adapterEncodingMatchesNativeCanonicalGolden() {
        val adapted = NativeProfileGlkv3Adapter.adapt(fixture())
        assertEquals(
            "Kotlin GLKv3 adapter bytes drifted from native glkv3::encode",
            golden,
            hex(Glkv3Encoder.encode(adapted)),
        )
    }

    @Test
    fun adapterCarriesRootSelectionTokens() {
        val adapted = NativeProfileGlkv3Adapter.adapt(fixture())
        assertEquals("5.15.189-android13-8-00016-g51bba4309aac", adapted.release)
        assertEquals(NativeProfileGlkv3Adapter.TERMINAL_ROOT_CHILD, adapted.terminal)
        assertEquals("cve_2026_43499", adapted.backend)
        assertEquals("multicast_waiter", adapted.route)
        assertEquals(Glkv3Encoder.SCHEMA_VERSION, adapted.schema)
    }

    @Test
    fun adapterAppliesNativeWireTypes() {
        val adapted = NativeProfileGlkv3Adapter.adapt(fixture())
        assertEquals(Glkv3Value.Bool(true), valueOf(adapted, "common", "safe_mode"))
        assertEquals(Glkv3Value.Bool(true), valueOf(adapted, "common", "vr_guard"))
        assertEquals(
            Glkv3Value.Bool(true),
            valueOf(adapted, "backend.cve_2026_43499.kernel", "compact_waiter"),
        )
        assertEquals(
            Glkv3Value.Int(-2),
            valueOf(adapted, "backend.cve_2026_43499.route.multicast_waiter", "waiter_off"),
        )
        assertEquals(Glkv3Value.UInt(5u), valueOf(adapted, "common", "kernel_major"))
        assertEquals(Glkv3Value.UInt(2u), valueOf(adapted, "backend.cve_2026_43499", "steps"))
    }

    @Test
    fun everyEmittedPathIsDeclaredInTheAdapterTable() {
        val adapted = NativeProfileGlkv3Adapter.adapt(fixture())
        val declared = NativeProfileGlkv3Adapter.declaredTypes()
        for (section in adapted.sections) {
            for (entry in section.entries) {
                val path = section.name + "." + entry.key
                assertTrue(
                    "adapter emitted an undeclared GLKv3 path: " + path,
                    declared.containsKey(path),
                )
            }
        }
    }

    @Test
    fun routeLessDocumentOmitsTheRouteRootKey() {
        val document = fixture().copy(
            backendKind = BackendKind.Cve2026_43284.wire.toUInt(),
            routeKind = 0u,
            cve2026_43284 = null,
        )
        val adapted = NativeProfileGlkv3Adapter.adapt(document)
        assertEquals("cve_2026_43284", adapted.backend)
        assertEquals(null, adapted.route)
        assertEquals(NativeProfileGlkv3Adapter.TERMINAL_ROOT_CHILD, adapted.terminal)
    }

    @Test
    fun adapterCarriesTheResolvedTerminalToken() {
        val document = fixture().copy(
            backendKind = BackendKind.Cve2026_43284.wire.toUInt(),
            routeKind = 0u,
            cve2026_43284 = null,
        )
        val adapted = NativeProfileGlkv3Adapter.adapt(
            document,
            NativeProfileGlkv3Adapter.TERMINAL_UMH_FORWARD,
        )
        assertEquals("cve_2026_43284", adapted.backend)
        assertEquals("umh_forward", adapted.terminal)
    }

    @Test
    fun documentCarriesOnlyItsSelectionOwnersPlusCommon() {
        val names43499 = NativeProfileGlkv3Adapter.adapt(fixture()).sections.map { it.name }.toSet()
        assertTrue("43499 must carry common", "common" in names43499)
        assertTrue("43499 must carry the platform ABI", "platform.abi.task_struct" in names43499)
        assertTrue(
            "43499 must carry its own route",
            "backend.cve_2026_43499.route.multicast_waiter" in names43499,
        )
        assertTrue(
            "43499 must carry the countermeasure section",
            "countermeasure.vivo_vr_guard" in names43499,
        )
        println("SEGMENTS_43499=" + names43499.sorted())

        val cve43284 = NativeProfileDocument(
            release = fixture().release,
            routeKind = 0u,
            kernelMajor = 0u,
            vrGuard = 0u,
            fallbackRoute = 0u,
            taskStruct = TaskStructOffsets(),
            cred = CredTemplate(),
            kernelOffset = KernelOffsetTable(),
            kernelPhysLoad = null,
            kernelPhysOffset = null,
            compactWaiter = null,
            vrGuardTracepointFuncs = null,
            kernelsnitchCollisions = null,
            mmStructSz = null,
            execution = ExecutionTuning(),
            safeMode = 1u,
            routeConfig = NoRouteConfig,
            steps = 3u,
            backendKind = BackendKind.Cve2026_43284.wire.toUInt(),
            cve2026_43284 = Cve2026_43284Config(kmi = 5150u),
        )
        val names43284 = NativeProfileGlkv3Adapter.adapt(cve43284).sections.map { it.name }.toSet()
        println("SEGMENTS_43284_BEFORE=" + cve43284.sections().map { it.name }.sorted())
        println("SEGMENTS_43284=" + names43284.sorted())
        assertEquals(setOf("common", "backend.cve_2026_43284"), names43284)
    }

    /**
     * The fully populated fixture glkv3_schema_test.cpp mirrors: every non-route
     * 43499 field present plus all seven active-route fields, with the same
     * distinctive values.
     */
    private fun fixture(): NativeProfileDocument = NativeProfileDocument(
        release = "5.15.189-android13-8-00016-g51bba4309aac",
        routeKind = RouteKind.MULTICAST_WAITER.wire,
        kernelMajor = 5u,
        vrGuard = 1u,
        fallbackRoute = 2u,
        taskStruct = TaskStructOffsets(prio = 101u),
        cred = CredTemplate(
            capsValue = 0x123456789abcdef0uL,
            ref0Image = 0x1111111111111111uL,
        ),
        kernelOffset = KernelOffsetTable(
            initTask = 34677760uL,
            vrSysExitTp = 0x2auL,
        ),
        kernelPhysLoad = 0xb000uL,
        kernelPhysOffset = 0xc000uL,
        compactWaiter = 1u.toUByte(),
        vrGuardTracepointFuncs = 0x20u,
        kernelsnitchCollisions = 7u,
        mmStructSz = 0x400u,
        execution = ExecutionTuning(),
        safeMode = 1u,
        routeConfig = MulticastConfig(
            geometry = MulticastGeometry(
                waiterOff = -2,
                bufferSize = 512u,
                taskOffset = 0x30u,
                lockOffset = 0x40u,
            ),
            attempts = 3u.toUByte(),
            armSequence = 4u.toUByte(),
            armHold = 20000u.toUShort(),
        ),
        steps = 2u,
        backendKind = BackendKind.Cve2026_43499.wire.toUInt(),
    )

    private fun valueOf(
        document: Glkv3Document,
        sectionName: String,
        key: String,
    ): Glkv3Value = document.sections
        .first { it.name == sectionName }
        .entries
        .first { it.key == key }
        .value

    private fun hex(bytes: ByteArray): String =
        bytes.joinToString("") { "%02x".format(it.toInt() and 0xFF) }
}
