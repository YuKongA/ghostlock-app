package com.ghostlock.app.data.run

import android.os.ParcelFileDescriptor
import android.system.Os
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.core.runhost.RunHostArtifactKind
import com.ghostlock.app.core.runhost.RunHostArtifactRequest
import com.ghostlock.app.core.runhost.RunHostIdentity
import com.ghostlock.app.core.rootscript.PublishedArtifactIdentity
import com.ghostlock.app.core.rootscript.ROOT_SCRIPT_HANDOFF_FILE_NAME
import com.ghostlock.app.core.rootscript.ROOT_SCRIPT_HANDOFF_MARKER
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import java.util.UUID
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith

/** Android gate for PFD staging and ordinary-file identity in the real platform filesystem. */
@RunWith(AndroidJUnit4::class)
class RootArtifactStagingAdapterTest {
    /** An already-open source object survives path replacement and publishes exact bytes and modes. */
    @Test
    fun stagesOpenedObjectAfterPathReplacementAndPublishesCatalog() {
        val sourceRoot = freshCacheDirectory("staging-source")
        val workspace = createWorkspace()
        val dataSource = File(sourceRoot, "profile.json")
        val executableSource = File(sourceRoot, "manager")
        val openedDataSource = File(sourceRoot, "profile-opened.json")
        val dataBytes = "{\"schema_version\":3}\n".toByteArray(Charsets.UTF_8)
        val replacementBytes = "replacement-path\n".toByteArray(Charsets.UTF_8)
        val executableBytes = "#!/system/bin/sh\nexit 0\n".toByteArray(Charsets.US_ASCII)
        dataSource.writeBytes(dataBytes)
        executableSource.writeBytes(executableBytes)
        val dataDescriptor = ParcelFileDescriptor.open(dataSource, ParcelFileDescriptor.MODE_READ_ONLY)
        val executableDescriptor = ParcelFileDescriptor.open(executableSource, ParcelFileDescriptor.MODE_READ_ONLY)

        try {
            Files.move(dataSource.toPath(), openedDataSource.toPath())
            dataSource.writeBytes(replacementBytes)
            val staged = RootArtifactStagingAdapter().stage(
                workspace,
                listOf(
                    OpenedRunInputArtifact(request("profile", RunHostArtifactKind.PROFILE, dataBytes), dataDescriptor),
                    OpenedRunInputArtifact(
                        request("root-manager", RunHostArtifactKind.EXECUTABLE, executableBytes),
                        executableDescriptor,
                    ),
                ),
            )

            assertFalse(dataDescriptor.fileDescriptor.valid())
            assertFalse(executableDescriptor.fileDescriptor.valid())
            assertArrayEquals(dataBytes, staged[0].file.readBytes())
            assertArrayEquals(executableBytes, staged[1].file.readBytes())
            assertEquals(DATA_MODE, Os.stat(staged[0].file.absolutePath).st_mode and MODE_MASK)
            assertEquals(EXECUTABLE_MODE, Os.stat(staged[1].file.absolutePath).st_mode and MODE_MASK)
            assertTrue(staged[0].identity.matches(staged[0].file))
            assertTrue(staged[1].identity.matches(staged[1].file))

            val catalog = RootArtifactStagingAdapter().publishCatalog(staged)
            assertEquals(listOf("profile", "root-manager"), catalog.keys.toList())
            assertEquals(staged[0].file.absolutePath, catalog.getValue("profile").path)
            assertEquals(staged[1].file.absolutePath, catalog.getValue("root-manager").path)
        } finally {
            runCatching { dataDescriptor.close() }
            runCatching { executableDescriptor.close() }
            assertTrue(workspace.deleteAfterTerminal())
            sourceRoot.deleteRecursively()
        }
    }

    /** Non-regular sources and mismatched metadata fail while the workspace owner can remove residue. */
    @Test
    fun rejectsPipeSizeDigestAndDuplicateCatalog() {
        val sourceRoot = freshCacheDirectory("staging-negative")
        val workspace = createWorkspace()
        val adapter = RootArtifactStagingAdapter()

        try {
            val pipe = ParcelFileDescriptor.createPipe()
            pipe[1].close()
            expectIllegalArgument {
                adapter.stage(
                    workspace,
                    listOf(
                        OpenedRunInputArtifact(
                            request("pipe", RunHostArtifactKind.DATA, ByteArray(0)),
                            pipe[0],
                        ),
                    ),
                )
            }
            assertFalse(pipe[0].fileDescriptor.valid())

            val sizeSource = File(sourceRoot, "size.bin").apply { writeBytes("size".toByteArray()) }
            expectIllegalArgument {
                adapter.stage(
                    workspace,
                    listOf(
                        OpenedRunInputArtifact(
                            request("bad-size", RunHostArtifactKind.DATA, sizeSource.readBytes()).copy(byteCount = 5L),
                            ParcelFileDescriptor.open(sizeSource, ParcelFileDescriptor.MODE_READ_ONLY),
                        ),
                    ),
                )
            }
            assertFalse(File(workspace.stagedInputs, "bad-size.bin").exists())

            val digestSource = File(sourceRoot, "digest.bin").apply { writeBytes("digest".toByteArray()) }
            expectIllegalArgument {
                adapter.stage(
                    workspace,
                    listOf(
                        OpenedRunInputArtifact(
                            request("bad-digest", RunHostArtifactKind.DATA, digestSource.readBytes())
                                .copy(sha256 = "0".repeat(64)),
                            ParcelFileDescriptor.open(digestSource, ParcelFileDescriptor.MODE_READ_ONLY),
                        ),
                    ),
                )
            }
            assertTrue(File(workspace.stagedInputs, "bad-digest.bin").isFile)

            val firstFile = File(workspace.stagedInputs, "first.bin").apply { writeText("first") }
            val secondFile = File(workspace.stagedInputs, "second.bin").apply { writeText("second") }
            val firstIdentity = PublishedArtifactIdentity.capture(firstFile)
            val secondIdentity = PublishedArtifactIdentity.capture(secondFile)
            val duplicateMetadata = RunHostArtifactRequest(
                logicalId = "duplicate",
                kind = RunHostArtifactKind.DATA,
                byteCount = firstIdentity.byteCount,
                sha256 = firstIdentity.sha256,
            )
            expectIllegalArgument {
                adapter.publishCatalog(
                    listOf(
                        StagedRunInputArtifact(duplicateMetadata, firstFile, firstIdentity),
                        StagedRunInputArtifact(
                            duplicateMetadata.copy(
                                byteCount = secondIdentity.byteCount,
                                sha256 = secondIdentity.sha256,
                            ),
                            secondFile,
                            secondIdentity,
                        ),
                    ),
                )
            }
        } finally {
            assertTrue(workspace.deleteAfterTerminal())
            sourceRoot.deleteRecursively()
        }
    }

    /** Android descriptor identity rejects byte mutation, inode replacement, and symlink replacement. */
    @Test
    fun identityRejectsMutationNewInodeAndSymlink() {
        val root = freshCacheDirectory("identity-negative")
        val artifact = File(root, "artifact.bin")
        val original = File(root, "artifact-original.bin")
        val target = File(root, "target.bin")
        val bytes = "same-bytes".toByteArray(Charsets.US_ASCII)

        try {
            artifact.writeBytes(bytes)
            val mutationIdentity = PublishedArtifactIdentity.capture(artifact)
            artifact.writeBytes("different!".toByteArray(Charsets.US_ASCII))
            assertFalse(mutationIdentity.matches(artifact))

            artifact.writeBytes(bytes)
            val inodeIdentity = PublishedArtifactIdentity.capture(artifact)
            Files.move(artifact.toPath(), original.toPath())
            artifact.writeBytes(bytes)
            assertFalse(inodeIdentity.matches(artifact))
            assertFalse(inodeIdentity.matchesObject(artifact))
            assertTrue(inodeIdentity.matches(original))

            val symlinkIdentity = PublishedArtifactIdentity.capture(artifact)
            assertTrue(artifact.delete())
            target.writeBytes(bytes)
            Files.createSymbolicLink(artifact.toPath(), target.toPath().fileName)
            assertFalse(symlinkIdentity.matches(artifact))
            assertFalse(symlinkIdentity.matchesObject(artifact))
        } finally {
            Files.deleteIfExists(artifact.toPath())
            root.deleteRecursively()
        }
    }

    /** Diagnose publishes the same fixed filename and marker consumed by common run_handoff. */
    @Test
    fun diagnosticArtifactUsesFixedHandoffContract() {
        val workspace = createWorkspace()
        val identity = RunHostIdentity(UUID.randomUUID().toString().replace("-", ""), 1L)
        try {
            val artifact = RootArtifactStagingAdapter().publishDiagnosticArtifact(workspace, identity)
            val script = File(workspace.rootScript, ROOT_SCRIPT_HANDOFF_FILE_NAME)
            val text = script.readText(Charsets.US_ASCII)

            assertEquals(script.absolutePath, artifact.scriptPath)
            assertTrue(artifact.scriptIdentity.matches(script))
            assertEquals(EXECUTABLE_MODE, Os.stat(script.absolutePath).st_mode and MODE_MASK)
            assertTrue(text.startsWith("#!/system/bin/sh\n$ROOT_SCRIPT_HANDOFF_MARKER\n"))
            assertTrue(text.contains("DIAGNOSE_STATUS=\$?"))
            assertTrue(text.contains("diagnose:shell:handoff-ok"))
            assertFalse(File(workspace.rootScript, "diagnostic-noop.sh").exists())
        } finally {
            assertTrue(workspace.deleteAfterTerminal())
        }
    }

    /** Creates one fresh app-owned workspace through the production allocator. */
    private fun createWorkspace(): AndroidRunWorkspace {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val identity = RunHostIdentity(UUID.randomUUID().toString().replace("-", ""), 1L)
        return AndroidRunWorkspace.createApp(context, identity, BACKEND_KEY)
    }

    /** Creates one unique cache directory for test-owned source objects. */
    private fun freshCacheDirectory(prefix: String): File {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        return File(context.cacheDir, "$prefix-${UUID.randomUUID()}").also { directory ->
            check(directory.mkdir()) { "cannot create test source directory" }
        }
    }

    /** Builds exact request metadata from the bytes expected through the opened descriptor. */
    private fun request(
        logicalId: String,
        kind: RunHostArtifactKind,
        bytes: ByteArray,
    ): RunHostArtifactRequest = RunHostArtifactRequest(
        logicalId = logicalId,
        kind = kind,
        byteCount = bytes.size.toLong(),
        sha256 = MessageDigest.getInstance("SHA-256").digest(bytes)
            .joinToString(separator = "") { byte ->
                (byte.toInt() and 0xff).toString(16).padStart(2, '0')
            },
    )

    /** Requires one [IllegalArgumentException] from the exercised staging boundary. */
    private fun expectIllegalArgument(block: () -> Unit) {
        try {
            block()
            fail("expected IllegalArgumentException")
        } catch (_: IllegalArgumentException) {}
    }

    private companion object {
        const val BACKEND_KEY: String = "DIAGNOSE"
        const val MODE_MASK: Int = 0x1ff
        const val DATA_MODE: Int = 0b100000000
        const val EXECUTABLE_MODE: Int = 0b101000000
    }
}
