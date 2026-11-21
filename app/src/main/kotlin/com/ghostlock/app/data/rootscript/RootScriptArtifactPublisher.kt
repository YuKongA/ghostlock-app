package com.ghostlock.app.data.rootscript

import android.os.Build
import android.system.Os
import android.system.OsConstants
import com.ghostlock.app.core.rootscript.ROOT_SCRIPT_HANDOFF_FILE_NAME
import com.ghostlock.app.core.rootscript.ROOT_SCRIPT_HANDOFF_MARKER
import com.ghostlock.app.core.rootscript.RootScriptArtifact
import com.ghostlock.app.core.rootscript.RootScriptExecutionPlan
import com.ghostlock.app.core.rootscript.RootScriptRenderer
import com.ghostlock.app.core.runhost.RunHostIdentity
import java.io.File
import java.io.FileOutputStream
import java.nio.charset.StandardCharsets
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardCopyOption
import java.nio.file.attribute.PosixFilePermission
import java.util.UUID

/** Android physical publisher for product and diagnostic root-script artifacts. */
class RootScriptArtifactPublisher(
    private val renderer: RootScriptRenderer = RootScriptRenderer(),
) {
    /**
     * Publishes one product script and pre-created mutable result object.
     *
     * @param workspace Fresh RunHost-owned root-script directory.
     * @param plan Fully normalized product execution plan.
     * @return Closed published artifact with captured script and result identities.
     * @throws IllegalStateException When exclusive publication cannot complete.
     */
    fun publish(workspace: File, plan: RootScriptExecutionPlan): RootScriptArtifact {
        plan.validate()
        check(workspace.isDirectory || workspace.mkdirs()) { "cannot create root-script workspace" }
        check(!Files.isSymbolicLink(workspace.toPath())) { "workspace must not be a symlink" }
        applyMode(workspace, WORKSPACE_MODE)
        val result = File(workspace, RESULT_FILE_NAME)
        check(!Files.exists(result.toPath(), LinkOption.NOFOLLOW_LINKS)) { "root result already exists" }
        val script = File(workspace, ROOT_SCRIPT_HANDOFF_FILE_NAME)
        val temporary = File(workspace, ".root-script.${UUID.randomUUID()}.tmp")
        var ownsResult = false
        var ownsTemporary = false
        var ownsScript = false
        try {
            createExclusiveEmptyFile(result, RESULT_MODE)
            ownsResult = true
            val resultIdentity = PublishedArtifactAccess.capture(result)
            writeExclusive(
                temporary,
                renderer.renderText(plan, result.absolutePath).toByteArray(StandardCharsets.UTF_8),
                RESULT_MODE,
            )
            ownsTemporary = true
            applyMode(temporary, SCRIPT_MODE)
            publishExclusiveMove(temporary, script)
            ownsTemporary = false
            ownsScript = true
            return RootScriptArtifact(
                runId = plan.runId,
                generation = plan.generation,
                scriptPath = script.absolutePath,
                resultPath = result.absolutePath,
                scriptIdentity = PublishedArtifactAccess.capture(script),
                resultIdentity = resultIdentity,
                retainedArtifacts = plan.retainedArtifacts.toList(),
            )
        } catch (problem: Throwable) {
            if (ownsTemporary) temporary.delete()
            if (ownsScript) script.delete()
            if (ownsResult) result.delete()
            throw IllegalStateException("cannot publish root script", problem)
        }
    }

    /**
     * Publishes the fixed inert handoff artifact required by a diagnostic Flow.
     *
     * @param workspace Fresh RunHost-owned root-script directory.
     * @param identity Exact run identity.
     * @return Diagnostic artifact that is never treated as a product result source.
     */
    fun publishDiagnostic(workspace: File, identity: RunHostIdentity): RootScriptArtifact {
        val script = File(workspace, ROOT_SCRIPT_HANDOFF_FILE_NAME)
        val result = File(workspace, DIAGNOSTIC_RESULT_FILE_NAME)
        var ownsScript = false
        var ownsResult = false
        try {
            writeExclusive(script, DIAGNOSTIC_SCRIPT.toByteArray(StandardCharsets.US_ASCII), SCRIPT_MODE)
            ownsScript = true
            writeExclusive(result, ByteArray(0), RESULT_MODE)
            ownsResult = true
            return RootScriptArtifact(
                runId = identity.runId,
                generation = identity.generation,
                scriptPath = script.absolutePath,
                resultPath = result.absolutePath,
                scriptIdentity = PublishedArtifactAccess.capture(script),
                resultIdentity = PublishedArtifactAccess.capture(result),
                retainedArtifacts = emptyList(),
            )
        } catch (problem: Throwable) {
            if (ownsResult) result.delete()
            if (ownsScript) script.delete()
            throw IllegalStateException("cannot publish diagnostic root script", problem)
        }
    }

    /** Publishes one completed sibling temporary through an atomic same-directory move. */
    private fun publishExclusiveMove(temporary: File, script: File) {
        check(!Files.exists(script.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            "root script already exists"
        }
        Files.move(temporary.toPath(), script.toPath(), StandardCopyOption.ATOMIC_MOVE)
    }

    /** Writes one new file through `O_EXCL|O_NOFOLLOW`, fsyncs, and closes it. */
    private fun writeExclusive(file: File, bytes: ByteArray, mode: Int) {
        val descriptor = Os.open(
            file.absolutePath,
            OsConstants.O_CREAT or OsConstants.O_EXCL or OsConstants.O_WRONLY or
                OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            mode,
        )
        try {
            FileOutputStream(descriptor).use { output ->
                output.write(bytes)
                output.flush()
                output.fd.sync()
            }
            applyMode(file, mode)
        } catch (problem: Throwable) {
            file.delete()
            throw problem
        }
    }

    /** Creates and fsyncs one new empty ordinary file without following links. */
    private fun createExclusiveEmptyFile(file: File, mode: Int) {
        val descriptor = Os.open(
            file.absolutePath,
            OsConstants.O_CREAT or OsConstants.O_EXCL or OsConstants.O_WRONLY or
                OsConstants.O_CLOEXEC or OsConstants.O_NOFOLLOW,
            mode,
        )
        try {
            FileOutputStream(descriptor).use { output -> output.fd.sync() }
            applyMode(file, mode)
        } catch (problem: Throwable) {
            file.delete()
            throw problem
        }
    }

    /** Applies the Android mode and mirrors it on Robolectric's host filesystem. */
    private fun applyMode(file: File, mode: Int) {
        Os.chmod(file.absolutePath, mode)
        if (Build.FINGERPRINT == "robolectric") {
            val permissions = buildSet {
                if (mode and 0b100000000 != 0) add(PosixFilePermission.OWNER_READ)
                if (mode and 0b010000000 != 0) add(PosixFilePermission.OWNER_WRITE)
                if (mode and 0b001000000 != 0) add(PosixFilePermission.OWNER_EXECUTE)
                if (mode and 0b000100000 != 0) add(PosixFilePermission.GROUP_READ)
                if (mode and 0b000010000 != 0) add(PosixFilePermission.GROUP_WRITE)
                if (mode and 0b000001000 != 0) add(PosixFilePermission.GROUP_EXECUTE)
                if (mode and 0b000000100 != 0) add(PosixFilePermission.OTHERS_READ)
                if (mode and 0b000000010 != 0) add(PosixFilePermission.OTHERS_WRITE)
                if (mode and 0b000000001 != 0) add(PosixFilePermission.OTHERS_EXECUTE)
            }
            Files.setPosixFilePermissions(file.toPath(), permissions)
        }
    }

    private companion object {
        const val WORKSPACE_MODE: Int = 0b111000000
        const val SCRIPT_MODE: Int = 0b101000000
        const val RESULT_MODE: Int = 0b110000000
        const val RESULT_FILE_NAME: String = "root-result.glr1"
        const val DIAGNOSTIC_RESULT_FILE_NAME: String = "diagnostic-result.empty"
        val DIAGNOSTIC_SCRIPT: String =
            "#!/system/bin/sh\n" +
                ROOT_SCRIPT_HANDOFF_MARKER + "\n" +
                "false\n" +
                "DIAGNOSE_STATUS=\$?\n" +
                "if [ \"\$DIAGNOSE_STATUS\" -ne 1 ]; then exit 126; fi\n" +
                "printf '%s\\n' '[I/0ms] diagnose:shell:handoff-ok'\n" +
                "exit 0\n"
    }
}
