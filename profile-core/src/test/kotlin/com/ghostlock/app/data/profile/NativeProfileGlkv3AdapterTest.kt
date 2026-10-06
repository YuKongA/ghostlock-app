package com.ghostlock.app.data.profile

import com.ghostlock.app.data.CredTemplate
import com.ghostlock.app.data.Cve2026_43284Config
import com.ghostlock.app.data.ExecutionTuning
import com.ghostlock.app.data.KernelOffsetTable
import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.QueueElement
import com.ghostlock.app.data.TaskStructOffsets
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.plugin.PluginEmission
import com.ghostlock.app.data.plugin.PluginManifestEntry
import com.ghostlock.app.data.plugin.PluginProbe
import com.ghostlock.app.data.plugin.PluginValue
import com.ghostlock.app.data.route.MulticastConfig
import com.ghostlock.app.data.route.MulticastGeometry
import com.ghostlock.app.data.route.NoRouteConfig
import com.ghostlock.app.data.route.RouteKind
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
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

    /** Native fixture bytes; see [Glkv3Golden] for the provenance. */
    private val golden = Glkv3Golden.SELECTION_HEX

    /** The same document with no selection declared (zero-new-bytes pin). */
    private val legacyGolden = Glkv3Golden.NO_SELECTION_HEX

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
        assertEquals("root_child", adapted.terminal)
        assertEquals("cve_2026_43499", adapted.backend)
        assertEquals("multicast_waiter", adapted.route)
        assertEquals(Glkv3Encoder.SCHEMA_VERSION, adapted.schema)
    }

    @Test
    fun pluginSectionsCarryTheRegistryIdentityAndDeclaredTypes() {
        val digest = "a".repeat(64)
        val descriptor = PluginProbe.parse(
            "host_abi\t1\n" +
                "countermeasures_root\tcountermeasures\n" +
                "host_stages\tpost_terminal\n" +
                "host_caps\tkernel_read\n" +
                "plugin\tdemo.plugin\t1.0\t1\t64\t" + digest + "\tpost_terminal\tkernel_read\n" +
                "param\tdemo.plugin\tthreshold\tuint\t0\t200\tdoc\n" +
                "param\tdemo.plugin\tmode\tstr\t0\tauto\tdoc\n" +
                "param\tdemo.plugin\tflag\tbool\t0\t1\tdoc\n" +
                "param\tdemo.plugin\tdelta\tint\t0\t0\tdoc\n" +
                "extract\tdemo.plugin\toffset\tuint\t1\t-\tfrom the boot image\n",
        )
        val entry = PluginManifestEntry(
            id = "demo.plugin",
            version = "1.0",
            abiVersion = 1u,
            sha256 = digest,
            modulePath = "demo.plugin/1.0/demo.plugin.so",
            enabled = true,
            stage = "post_terminal",
            importedAtMs = 7L,
        )
        val emission = requireNotNull(
            PluginEmission.of(
                entry,
                descriptor,
                mapOf(
                    "threshold" to PluginValue.UInt(7u),
                    "mode" to PluginValue.Str("manual"),
                    "flag" to PluginValue.Bool(false),
                    "delta" to PluginValue.Int(-3L),
                ),
                extracts = mapOf("offset" to PluginValue.UInt(4096u)),
            ),
        )
        val adapted = NativeProfileGlkv3Adapter.adapt(fixture().copy(plugins = listOf(emission)))
        /* Canonical shape (plugin/schema.hpp + plugin/wire.cpp): ONE section
         * named `plugin`, every key spelled `<id>.<field>`; a per-plugin
         * `plugin.<id>` section is never emitted. */
        assertTrue(adapted.sections.none { it.name.startsWith("plugin.") })
        val pluginSection = adapted.sections.first { it.name == "plugin" }
        assertEquals(
            setOf(
                "demo.plugin.enabled",
                "demo.plugin.stage",
                "demo.plugin.module_path",
                "demo.plugin.module_hash",
                "demo.plugin.params.threshold",
                "demo.plugin.params.mode",
                "demo.plugin.params.flag",
                "demo.plugin.params.delta",
                "demo.plugin.extract.offset",
            ),
            pluginSection.entries.map { it.key }.toSet(),
        )
        assertEquals(Glkv3Value.Bool(true), valueOf(adapted, "plugin", "demo.plugin.enabled"))
        assertEquals(
            Glkv3Value.Str("post_terminal"),
            valueOf(adapted, "plugin", "demo.plugin.stage"),
        )
        assertEquals(
            Glkv3Value.Str("demo.plugin/1.0/demo.plugin.so"),
            valueOf(adapted, "plugin", "demo.plugin.module_path"),
        )
        assertEquals(Glkv3Value.Str(digest), valueOf(adapted, "plugin", "demo.plugin.module_hash"))
        /* Parameters are typed by the descriptor, and extract.* is not emitted. */
        assertEquals(Glkv3Value.UInt(7u), valueOf(adapted, "plugin", "demo.plugin.params.threshold"))
        assertEquals(Glkv3Value.Str("manual"), valueOf(adapted, "plugin", "demo.plugin.params.mode"))
        assertEquals(Glkv3Value.Bool(false), valueOf(adapted, "plugin", "demo.plugin.params.flag"))
        assertEquals(Glkv3Value.Int(-3L), valueOf(adapted, "plugin", "demo.plugin.params.delta"))
        /* P2: only the declared, resolved extract key rides, in its own group. */
        assertEquals(Glkv3Value.UInt(4096u), valueOf(adapted, "plugin", "demo.plugin.extract.offset"))
    }

    @Test
    fun adapterAppliesNativeWireTypes() {
        val adapted = NativeProfileGlkv3Adapter.adapt(fixture())
        /* HOCON refactor: root scalars, and vr_guard is gone from the wire. */
        assertEquals(true, adapted.safeMode)
        assertEquals(
            Glkv3Value.Bool(true),
            valueOf(adapted, "backend.cve_2026_43499.kernel", "compact_waiter"),
        )
        assertEquals(
            Glkv3Value.Int(-2),
            valueOf(adapted, "backend.cve_2026_43499.route.multicast_waiter", "waiter_off"),
        )
        assertEquals(5uL, adapted.kernelMajor)
        /* M5: the combination token no longer rides the wire; the backend owner
         * carries the queue-level route (native fixture line 13). */
        assertEquals(
            Glkv3Value.Str("multicast_waiter"),
            valueOf(adapted, "backend.cve_2026_43499", "route"),
        )
    }

    @Test
    fun everyEmittedPathIsDeclaredInTheAdapterTable() {
        val adapted = NativeProfileGlkv3Adapter.adapt(fixture())
        for (section in adapted.sections) {
            for (entry in section.entries) {
                val path = section.name + "." + entry.key
                assertTrue(
                    "adapter emitted an undeclared GLKv3 path: " + path,
                    NativeProfileGlkv3Adapter.declaredWire(path) != null,
                )
            }
        }
    }

    @Test
    fun routeLessDocumentOmitsTheRouteRootKey() {
        val document = fixture().copy(
            backendKind = BackendKind.Cve2026_43284.wire.toUInt(),
            routeKind = 0u,
            combination = requireNotNull(CombinationCatalog.resolve("umh")) { "umh" },
            cve2026_43284 = null,
            /* No route axis: the M2 queue selection must be cleared too. */
            queueRoute = null,
            stepQueue = null,
            experimental = null,
        )
        val adapted = NativeProfileGlkv3Adapter.adapt(document)
        assertEquals("cve_2026_43284", adapted.backend)
        assertEquals(null, adapted.route)
        assertEquals("umh_forward", adapted.terminal)
    }

    @Test
    fun adapterDerivesTheTerminalTokenFromTheCombinationToken() {
        val document = fixture().copy(
            backendKind = BackendKind.Cve2026_43284.wire.toUInt(),
            routeKind = 0u,
            combination = requireNotNull(CombinationCatalog.resolve("umh")) { "umh" },
            cve2026_43284 = null,
            queueRoute = null,
            stepQueue = null,
            experimental = null,
        )
        val adapted = NativeProfileGlkv3Adapter.adapt(document)
        assertEquals("cve_2026_43284", adapted.backend)
        assertEquals("umh_forward", adapted.terminal)
        assertEquals(null, adapted.route)
    }

    @Test
    fun wireCarriesNoStepsCombinationToken() {
        /* M5: the combination token is an INTERNAL normalisation key only. The wire
         * carries the queue + route, so the token must never be materialised back. */
        val adapted = NativeProfileGlkv3Adapter.adapt(fixture())
        val steps = adapted.sections.flatMap { it.entries }.filter { it.key == "steps" }
        assertTrue(
            "no combination token may ride the wire: " + steps.map { it.value },
            steps.isEmpty(),
        )
    }
    @Test
    fun documentCarriesOnlyItsSelectionOwnersAndNoLegacyOwners() {
        val document = fixture()
        val adapted = NativeProfileGlkv3Adapter.adapt(document)
        val names43499 = adapted.sections.map { it.name }.toSet()
        /* HOCON refactor: the ABI tables belong to the 43499 backend, the root
         * scalars are ROOT values (never a section), and the common /
         * platform / countermeasure owners are gone. */
        assertTrue(
            "43499 must carry its ABI tables",
            "backend.cve_2026_43499.abi.task_struct" in names43499,
        )
        assertTrue(
            "43499 must carry its own route",
            "backend.cve_2026_43499.route.multicast_waiter" in names43499,
        )
        assertTrue("no legacy owners may ride the wire", names43499.none {
            it == "common" || it == "platform.abi" || it.startsWith("platform.abi.") ||
                it.startsWith("countermeasure.")
        })
        assertEquals(5uL, adapted.kernelMajor)
        assertEquals(1u.toULong(), adapted.safeMode?.let { if (it) 1uL else 0uL })
        println("SEGMENTS_43499=" + names43499.sorted())

        val cve43284 = NativeProfileDocument(
            release = fixture().release,
            routeKind = 0u,
            kernelMajor = 0u,
            taskStruct = TaskStructOffsets(),
            cred = CredTemplate(),
            kernelOffset = KernelOffsetTable(),
            kernelPhysLoad = null,
            kernelPhysOffset = null,
            compactWaiter = null,
            kernelsnitchCollisions = null,
            mmStructSz = null,
            execution = ExecutionTuning(),
            safeMode = 1u,
            routeConfig = NoRouteConfig,
            combination = requireNotNull(CombinationCatalog.resolve("umh")) { "umh" },
            backendKind = BackendKind.Cve2026_43284.wire.toUInt(),
            /* kmi / lkm_path / carrier_path are native-side conventions now. */
            cve2026_43284 = Cve2026_43284Config(lateLoadArgs = 0uL),
        )
        val names43284 = NativeProfileGlkv3Adapter.adapt(cve43284).sections.map { it.name }.toSet()
        println("SEGMENTS_43284_BEFORE=" + cve43284.sections().map { it.name }.sorted())
        println("SEGMENTS_43284=" + names43284.sorted())
        assertEquals(
            setOf(
                /* M5: the token is no longer emitted, so the plain 43284 owner has no
                 * entries and the empty section is not materialised - only its
                 * execution tuning rides the wire. */
                "backend.cve_2026_43284.execution",
            ),
            names43284,
        )
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
        kernelMinor = 15u,
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
        combination = requireNotNull(CombinationCatalog.resolve("mcast_rootchild")) { "mcast_rootchild" },
        backendKind = BackendKind.Cve2026_43499.wire.toUInt(),
        /* M2 queue selection, mirroring the native fixture document. */
        queueRoute = "multicast_waiter",
        stepQueue = listOf(QueueElement(step = "w1")),
        experimental = true,
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

    @Test
    fun `the manifest width column is fail-closed`() {
        assertNull(NativeProfileGlkv3Adapter.parseWidthColumn("-", "line"))
        assertEquals(1, NativeProfileGlkv3Adapter.parseWidthColumn("1", "line"))
        assertEquals(4, NativeProfileGlkv3Adapter.parseWidthColumn("4", "line"))
        assertEquals(8, NativeProfileGlkv3Adapter.parseWidthColumn("8", "line"))
        /* The manifest is the single authority for width and it is a hard
         * validation input, so a missing/garbled column must fail closed
         * instead of defaulting silently. */
        for (bad in listOf("0", "3", "16", "", "x", "-1")) {
            val thrown = assertThrows(IllegalArgumentException::class.java) {
                NativeProfileGlkv3Adapter.parseWidthColumn(bad, "line")
            }
            assertTrue(
                "width " + bad + " must be rejected",
                thrown.message.orEmpty().contains("width must be one of"),
            )
        }
    }

    @Test
    fun `the bundled manifest declares widths consistent with its wire kinds`() {
        val widths = NativeProfileGlkv3Adapter.declaredWidths()
        val types = NativeProfileGlkv3Adapter.declaredTypeNames()
        assertTrue("manifest must declare fields", widths.isNotEmpty())
        for ((path, width) in widths) {
            val wire = types.getValue(path)
            /* A union spelling resolves its concrete kind from a plugin
             * descriptor, so it is out of scope for this guard. */
            if (wire.contains("|")) continue
            when (wire) {
                "str", "array" -> assertNull(path + " is width-less", width)
                /* Native ruling (bool convention): a STATIC declaration exports
                 * its real width -- bool => 1, uint/int => 1/2/4/8, str/array =>
                 * "-" -- while a descriptor-owned DYNAMIC family (params.* and
                 * extract.*) exports "-" and is skipped above as a union spelling.
                 * So a bool row must be exactly 1; anything else is a manifest
                 * defect and fails closed here. */
                "bool" -> assertEquals(path + " must be 1 bit wide", 1, width)
                else -> assertTrue(
                    path + " declares an illegal width " + width,
                    width != null && width in setOf(1, 2, 4, 8),
                )
            }
        }
    }


    @Test
    fun `declaredWidth is the single-path view of the manifest widths`() {
        val types = NativeProfileGlkv3Adapter.declaredTypeNames()
        val widths = NativeProfileGlkv3Adapter.declaredWidths()
        assertTrue("manifest must declare fields", widths.isNotEmpty())
        for ((path, width) in widths) {
            assertEquals(
                path + " disagrees between declaredWidth and declaredWidths",
                width,
                NativeProfileGlkv3Adapter.declaredWidth(path),
            )
        }
        val widthless = types.entries
            .filter { it.value == "str" || it.value == "array" }
            .map { it.key }
        assertTrue("the manifest must declare width-less kinds", widthless.isNotEmpty())
        for (path in widthless) {
            assertNull(
                path + " must be width-less",
                NativeProfileGlkv3Adapter.declaredWidth(path),
            )
        }
        /* An undeclared path has no width: no implicit prefix rule. */
        assertNull(
            "an undeclared path must have no width",
            NativeProfileGlkv3Adapter.declaredWidth("backend.cve_2026_43499.no_such_key"),
        )
    }

    @Test
    fun `queue selection rides the backend owner in the native wire shape`() {
        val adapted = NativeProfileGlkv3Adapter.adapt(fixture())
        assertEquals(
            Glkv3Value.Str("multicast_waiter"),
            valueOf(adapted, "backend.cve_2026_43499", "route"),
        )
        assertEquals(
            Glkv3Value.Bool(true),
            valueOf(adapted, "backend.cve_2026_43499", "experimental"),
        )
        assertEquals(
            Glkv3Value.Array(listOf(Glkv3Value.Map(listOf("step" to Glkv3Value.Str("w1"))))),
            valueOf(adapted, "backend.cve_2026_43499", "queue"),
        )
        /* The queue-level route token (key `route`) and the route geometry
         * (section `...route.<branch>`) are distinct wire keys: both ride, so
         * the canonical `queue_route` separation costs no wire shape. */
        assertTrue(
            "the route geometry must still ride",
            adapted.sections.any { it.name == "backend.cve_2026_43499.route.multicast_waiter" },
        )
    }

    @Test
    fun `an undeclared queue selection adds no bytes`() {
        val document = fixture().copy(queueRoute = null, stepQueue = null, experimental = null)
        assertEquals(
            "an undeclared queue/route/experimental must keep the pre-M2 bytes",
            legacyGolden,
            hex(Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(document))),
        )
    }

    @Test
    fun `the adapted document matches the native fixture field for field`() {
        assertEquals(nativeFixtureDump(), dump(NativeProfileGlkv3Adapter.adapt(fixture())))
    }

    private fun nativeFixtureDump(): String =
        checkNotNull(javaClass.getResourceAsStream(NATIVE_FIXTURE_RESOURCE)) {
            "missing native fixture resource: " + NATIVE_FIXTURE_RESOURCE
        }.bufferedReader().use { it.readText() }

    /**
     * Field-by-field rendering of an adapted document in the native
     * `glkv3_schema_test --dump-fixture` format: the root keys in the native
     * order, then every section entry as `path<TAB>wire<TAB>value` in canonical
     * (UTF-8 byte) order. It is compared against the native dump itself, so a
     * one-sided fixture change turns this red and names the diverging path.
     */
    private fun dump(document: Glkv3Document): String {
        val types = NativeProfileGlkv3Adapter.declaredTypeNames()
        val out = StringBuilder()
        val root = linkedMapOf<String, String>()
        root["schema"] = "uint\t" + document.schema
        document.release?.let { root["release"] = "str\t" + it }
        document.terminal?.let { root["terminal"] = "str\t" + it }
        document.backend?.let { root["backend"] = "str\t" + it }
        document.route?.let { root["route"] = "str\t" + it }
        document.kernelMajor?.let { root["kernel_major"] = "uint\t" + it }
        document.kernelMinor?.let { root["kernel_minor"] = "uint\t" + it }
        document.safeMode?.let { root["safe_mode"] = "bool\t" + it }
        out.append("# root keys\n")
        for (key in ROOT_KEY_ORDER) {
            root[key]?.let { out.append(key).append('\t').append(it).append('\n') }
        }
        out.append("# sections\n")
        val sections = document.sections.sortedWith { a, b ->
            Glkv3Encoder.compareUtf8Bytes(a.name, b.name)
        }
        for (section in sections) {
            val entries = section.entries.sortedWith { a, b ->
                Glkv3Encoder.compareUtf8Bytes(a.key, b.key)
            }
            for (entry in entries) {
                val path = section.name + "." + entry.key
                val wire = types[path] ?: error("no manifest declaration for " + path)
                out.append(path).append('\t').append(wire).append('\t')
                    .append(renderWireValue(entry.value)).append('\n')
            }
        }
        return out.toString()
    }

    private fun renderWireValue(value: Glkv3Value): String = when (value) {
        is Glkv3Value.UInt -> value.value.toString()
        is Glkv3Value.Int -> value.value.toString()
        is Glkv3Value.Bool -> value.value.toString()
        is Glkv3Value.Str -> value.value
        is Glkv3Value.Array -> ""
        is Glkv3Value.Bin -> value.value.joinToString("") { "%02x".format(it.toInt() and 0xFF) }
        /* The wire model has no bare-map section entry (the queue rides as an
         * array of maps), so this is a defect, not a rendering case. */
        is Glkv3Value.Map -> error("a GLKv3 section entry is never a bare map")
    }

    private companion object {
        /** Native `make -C src glkv3-golden-fixture` output, checked in below. */
        const val NATIVE_FIXTURE_RESOURCE = "/glkv3-native-fixture.tsv"

        /** The native dump's root-key order (not the encoder's sorted order). */
        val ROOT_KEY_ORDER = listOf(
            "schema", "release", "terminal", "backend", "route",
            "kernel_major", "kernel_minor", "safe_mode",
        )
    }

}