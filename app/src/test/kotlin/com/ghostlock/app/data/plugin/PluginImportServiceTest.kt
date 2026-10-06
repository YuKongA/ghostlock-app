package com.ghostlock.app.data.plugin

import java.io.File
import java.io.RandomAccessFile
import java.nio.file.Files
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * P1 import pipeline: order is the contract — bound, local hash, probe under
 * that digest, self-consistency, atomic install, registry. Every rejection must
 * leave the no-backup root untouched.
 */
class PluginImportServiceTest {

    private fun root(): File = Files.createTempDirectory("glk-plugin-import").toFile()

    private fun module(bytes: ByteArray): File {
        val file = Files.createTempFile("glk-plugin-src", ".so").toFile()
        file.writeBytes(bytes)
        return file
    }

    private fun probeText(
        id: String = "demo.plugin",
        version: String = "1.0",
        size: Long,
        sha: String,
        extra: String = "",
    ): String = "host_abi\t1\n" +
        "countermeasures_root\tcountermeasures\n" +
        "host_stages\tpost_terminal\n" +
        "host_caps\tkernel_read\n" +
        "plugin\t" + id + "\t" + version + "\t1\t" + size + "\t" + sha +
        "\tpost_terminal\tkernel_read\n" +
        "param\t" + id + "\tthreshold\tuint\t0\t200\tdoc\n" +
        extra

    /** P0: the probe's single declared stage is pinned in the registry at import. */
    @Test
    fun `import pins the plugin's only declared stage`() {
        val bytes = ByteArray(64) { it.toByte() }
        val source = module(bytes)
        val home = root()
        val invoker = PluginProbeInvoker { _, _ ->
            PluginProbeOutput(0, probeText(size = 64, sha = Sha256.bytes(bytes)), "")
        }
        val imported = result(home, invoker, source, "m.so") as PluginImportResult.Imported
        assertEquals("post_terminal", imported.entry.stage)
    }

    /**
     * P0: a re-description failure carries the probe's own reason. The old
     * `describe` returned null for every failure, so the page (and the document
     * build) could only say "could not be described".
     */
    @Test
    fun `describe reports the probe's reason when the module cannot be described`() = runBlocking {
        val home = root()
        val entry = manifestEntry(Sha256.bytes(ByteArray(64)))
        val service = PluginImportService(
            homeDir = home,
            store = PluginStore(File(home, PluginPaths.COUNTERMEASURES_ROOT)),
            invoker = PluginProbeInvoker { _, _ -> PluginProbeOutput(3, "", "probe: cannot dlopen\n") },
        )
        val installed = installedFile(home, "countermeasures/demo.plugin/1.0/demo.plugin.so")
        installed.parentFile!!.mkdirs()
        installed.writeBytes(ByteArray(64))
        assertEquals(
            PluginDescribeResult.Failed("probe: cannot dlopen"),
            service.describe(entry),
        )
    }

    /** The reason names the path when the registry row points at a missing file. */
    @Test
    fun `describe names a missing module file`() = runBlocking {
        val home = root()
        val entry = manifestEntry(Sha256.bytes(ByteArray(64)))
        val service = PluginImportService(
            homeDir = home,
            store = PluginStore(File(home, PluginPaths.COUNTERMEASURES_ROOT)),
            invoker = PluginProbeInvoker { _, _ -> PluginProbeOutput(0, "", "") },
        )
        val installed = installedFile(home, "countermeasures/demo.plugin/1.0/demo.plugin.so")
        assertEquals(
            PluginDescribeResult.Failed("the module file is missing: " + installed.absolutePath),
            service.describe(entry),
        )
    }

    private fun manifestEntry(sha: String) = PluginManifestEntry(
        id = "demo.plugin",
        version = "1.0",
        abiVersion = 1u,
        sha256 = sha,
        modulePath = "demo.plugin/1.0/demo.plugin.so",
        enabled = true,
        stage = null,
        importedAtMs = 7L,
    )

    /** [home] plays the GHOSTLOCK_HOME role: the service creates the root under it. */
    private fun result(
        home: File,
        invoker: PluginProbeInvoker,
        source: File,
        displayName: String?,
    ): PluginImportResult = runBlocking {
        PluginImportService(
            homeDir = home,
            store = PluginStore(File(home, PluginPaths.COUNTERMEASURES_ROOT)),
            invoker = invoker,
            now = { 42L },
        ).import(source, displayName)
    }

    private fun installedFile(home: File, relative: String): File = File(home, relative)

    @Test
    fun `a described module is installed atomically and registered disabled`() {
        val bytes = ByteArray(64) { it.toByte() }
        val source = module(bytes)
        val root = root()
        var expectedSeen: String? = null
        val invoker = PluginProbeInvoker { path, expected ->
            expectedSeen = expected
            assertEquals(source.absolutePath, path)
            PluginProbeOutput(0, probeText(size = 64, sha = Sha256.bytes(bytes)), "")
        }
        val outcome = result(root, invoker, source, "my_module.so")
        val imported = outcome as PluginImportResult.Imported
        assertEquals(Sha256.bytes(bytes), expectedSeen)
        assertEquals("demo.plugin", imported.entry.id)
        assertEquals("demo.plugin/1.0/my_module.so", imported.entry.modulePath)
        assertFalse(imported.entry.enabled)
        assertEquals(42L, imported.entry.importedAtMs)
        assertEquals(Sha256.bytes(bytes), imported.entry.sha256)
        val installed = installedFile(root, "countermeasures/demo.plugin/1.0/my_module.so")
        assertTrue("module not installed at " + installed.path, installed.isFile)
        assertEquals(bytes.toList(), installed.readBytes().toList())
        assertFalse(
            File(root, "countermeasures/demo.plugin/1.0/my_module.so.tmp").exists(),
        )
        val store = PluginStore(File(root, PluginPaths.COUNTERMEASURES_ROOT))
        assertEquals(listOf("demo.plugin"), store.load().map { it.id })
    }

    @Test
    fun `the picked name falls back to the plugin id when it is not usable`() {
        val bytes = ByteArray(8)
        val source = module(bytes)
        val root = root()
        val invoker = PluginProbeInvoker { _, _ ->
            PluginProbeOutput(0, probeText(size = 8, sha = Sha256.bytes(bytes)), "")
        }
        val imported = result(root, invoker, source, "not-a-module.apk") as PluginImportResult.Imported
        assertEquals("demo.plugin/1.0/demo.plugin.so", imported.entry.modulePath)
    }

    @Test
    fun `a probe rejection reports the reason and installs nothing`() {
        val root = root()
        val invoker = PluginProbeInvoker { _, _ ->
            PluginProbeOutput(1, "reject\t-\tHashMismatch\n", "HashMismatch: digest differs")
        }
        val rejected = result(root, invoker, module(ByteArray(16)), "x.so")
            as PluginImportResult.Rejected
        assertEquals("HashMismatch", rejected.reason)
        val leftovers = root.listFiles()?.toList().orEmpty()
        assertTrue("a rejection must install nothing: " + leftovers, leftovers.isEmpty())
    }

    @Test
    fun `a probe timeout is reported without a reason line`() {
        val invoker = PluginProbeInvoker { _, _ -> PluginProbeOutput(-1, "", "") }
        val rejected = result(root(), invoker, module(ByteArray(4)), "x.so")
            as PluginImportResult.Rejected
        assertEquals("the probe timed out", rejected.reason)
    }

    @Test
    fun `self-inconsistent reports are rejected`() {
        val bytes = ByteArray(32)
        val sha = Sha256.bytes(bytes)
        /* glk_module.size is the DESCRIPTOR size, not the file length: a descriptor that
         * cannot fit inside the file we hold is impossible (33 > 32). */
        val oversizeDescriptor = PluginProbeInvoker { _, _ ->
            PluginProbeOutput(0, probeText(size = 33, sha = sha), "")
        }
        assertTrue(
            (result(root(), oversizeDescriptor, module(bytes), "x.so") as PluginImportResult.Rejected)
                .reason.contains("impossible descriptor size"),
        )
        val wrongHash = PluginProbeInvoker { _, _ ->
            PluginProbeOutput(0, probeText(size = 32, sha = "0".repeat(64)), "")
        }
        assertTrue(
            (result(root(), wrongHash, module(bytes), "x.so") as PluginImportResult.Rejected)
                .reason.contains("different SHA-256"),
        )
        val badId = PluginProbeInvoker { _, _ ->
            PluginProbeOutput(0, probeText(id = "Bad_Id", size = 32, sha = sha), "")
        }
        assertTrue(
            (result(root(), badId, module(bytes), "x.so") as PluginImportResult.Rejected)
                .reason.contains("plugin id"),
        )
    }

    @Test
    fun `a descriptor smaller than the file is accepted`() {
        /* Regression for the device report "the probe reports 80 bytes, the picked file is
         * 8992": the import compared the ABI descriptor size (sizeof(glk_module)) to the
         * picked file's length, which rejected every real plugin. */
        val bytes = ByteArray(4096)
        val invoker = PluginProbeInvoker { _, _ ->
            PluginProbeOutput(0, probeText(size = 80, sha = Sha256.bytes(bytes)), "")
        }
        assertTrue(result(root(), invoker, module(bytes), "glk_probe.so") is PluginImportResult.Imported)
    }

    @Test
    fun `a descriptor the host would reject is refused`() {
        val bytes = ByteArray(16)
        val invoker = PluginProbeInvoker { _, _ ->
            PluginProbeOutput(
                0,
                probeText(
                    size = 16,
                    sha = Sha256.bytes(bytes),
                    extra = "reject\tdemo.plugin\treserved capability\n",
                ),
                "",
            )
        }
        val rejected = result(root(), invoker, module(bytes), "x.so")
            as PluginImportResult.Rejected
        assertTrue(rejected.reason.contains("reserved capability"))
    }

    @Test
    fun `an oversized module is refused before the probe runs`() {
        val source = Files.createTempFile("glk-oversize", ".so").toFile()
        RandomAccessFile(source, "rw").use { it.setLength(PluginPaths.MAX_MODULE_BYTES + 1L) }
        var probed = false
        val invoker = PluginProbeInvoker { _, _ ->
            probed = true
            PluginProbeOutput(0, "", "")
        }
        val rejected = result(root(), invoker, source, "x.so") as PluginImportResult.Rejected
        assertTrue(rejected.reason.contains("MiB"))
        assertFalse(probed)
    }

    @Test
    fun `a missing source file is refused`() {
        val invoker = PluginProbeInvoker { _, _ -> PluginProbeOutput(0, "", "") }
        val rejected = result(root(), invoker, File("/nonexistent/plugin.so"), "x.so")
            as PluginImportResult.Rejected
        assertTrue(rejected.reason.contains("not readable"))
    }
}
