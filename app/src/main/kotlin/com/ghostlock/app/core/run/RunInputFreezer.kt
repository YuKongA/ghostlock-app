package com.ghostlock.app.core.run

import android.content.Context
import com.ghostlock.app.data.run.PackagedBackendArtifactResolver
import com.ghostlock.app.data.run.RootManagerArtifactResolver
import com.ghostlock.app.domain.backend.BackendSpec
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.finish.FinishRepository
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadRepository
import com.ghostlock.app.domain.run.FrozenArtifactInvocation
import com.ghostlock.app.domain.run.FrozenRunInput
import com.ghostlock.app.domain.run.ProfileSnapshot
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.app.domain.run.RunInputArtifactKind
import java.io.File
import java.util.UUID

/**
 * Single application boundary that freezes backend/profile/Payload/Finish and opened source objects.
 *
 * @param context Application context used for the private immutable profile fixture directory.
 * @param payloadRepository Canonical version-three Payload authority.
 * @param finishRepository Canonical version-three Finish authority.
 * @param packagedCarrierResolver Backend declaration to packaged same-object carrier resolver.
 * @param rootManagerResolver Installed manager executable and exact argv resolver.
 */
class RunInputFreezer(
    context: Context,
    private val payloadRepository: PayloadRepository,
    private val finishRepository: FinishRepository,
    private val packagedCarrierResolver: PackagedBackendArtifactResolver,
    private val rootManagerResolver: RootManagerArtifactResolver,
) {
    private val lock = Any()
    private val sourceDirectory = File(context.applicationContext.noBackupFilesDir, "run-input")
    private var generation: Long = 0L

    /**
     * Freezes one run and returns ownership of every master descriptor in one closeable lease.
     *
     * @param spec Exact selected backend spec.
     * @return Immutable run input plus opaque-token descriptor ownership.
     * @throws IllegalStateException When an input, carrier, or manager object cannot be fixed exactly once.
     */
    fun freeze(spec: BackendSpec): FrozenRunInputLease {
        val identity = nextIdentity()
        val profileFile = publishProfileFixture(identity.first, spec)
        val sources = mutableListOf<OpenedRunInputSource>()
        try {
            val payload = when (spec.runCapability) {
                RunCapability.DIAGNOSTIC_ONLY -> PayloadDefinition(items = emptyList())
                RunCapability.PRODUCT_ROOT -> payloadRepository.read()
                RunCapability.UNRESOLVED -> error("cannot freeze unresolved backend capability")
            }
            val finish = when (spec.runCapability) {
                RunCapability.DIAGNOSTIC_ONLY -> FinishDefinition()
                RunCapability.PRODUCT_ROOT -> finishRepository.read()
                RunCapability.UNRESOLVED -> error("cannot freeze unresolved backend capability")
            }
            val profileSource = OpenedRunInputSource.open(
                file = profileFile,
                logicalId = PROFILE_ARTIFACT_ID,
                kind = RunInputArtifactKind.PROFILE,
                ownedSource = true,
            )
            sources += profileSource
            val invocations = mutableListOf<FrozenArtifactInvocation>()
            if (spec.runCapability == RunCapability.PRODUCT_ROOT) {
                sources += packagedCarrierResolver.resolve(spec.key)
                val rootManagerItems = payload.enabledItems().filter { item ->
                    item.kind == PayloadKind.RootManager
                }
                if (rootManagerItems.isNotEmpty()) {
                    val resolved = rootManagerResolver.resolve(payload)
                    sources += resolved.source
                    rootManagerItems.forEach { item ->
                        invocations += FrozenArtifactInvocation(
                            itemId = item.id,
                            artifactId = resolved.source.artifact.logicalId,
                            args = resolved.args.toList(),
                        )
                    }
                }
                require(payload.enabledItems().none { item ->
                    item.kind == PayloadKind.Lkm ||
                        (item.kind == PayloadKind.Command && item.scriptMode == 1)
                }) { "document-backed Payload artifact resolver is not connected" }
                require(finish.items.none { item -> item.kind == FinishKind.Script }) {
                    "document-backed Finish artifact resolver is not connected"
                }
            }
            val input = FrozenRunInput(
                runId = identity.first,
                generation = identity.second,
                backendKey = spec.key,
                profile = ProfileSnapshot(
                    artifactId = PROFILE_ARTIFACT_ID,
                    schemaVersion = ProfileSnapshot.CURRENT_SCHEMA_VERSION,
                    sha256 = profileSource.artifact.sha256,
                ),
                payload = payload,
                finish = finish,
                inputArtifacts = sources.map(OpenedRunInputSource::artifact),
                artifactInvocations = invocations,
            )
            return FrozenRunInputLease(input, sources)
        } catch (problem: Throwable) {
            sources.forEach { source -> runCatching { source.master.close() } }
            runCatching { if (profileFile.exists()) profileFile.delete() }
            throw problem
        }
    }

    /** Allocates a UUID run id and a positive process-monotonic generation. */
    private fun nextIdentity(): Pair<String, Long> = synchronized(lock) {
        check(generation < Long.MAX_VALUE) { "run generation exhausted" }
        generation += 1L
        UUID.randomUUID().toString().replace("-", "") to generation
    }

    /**
     * Publishes the minimal canonical profile-v3 fixture used by diagnostic R3 runs.
     *
     * @note Product enablement remains blocked on the separate canonical profile-v3 encoder/decoder closure.
     */
    private fun publishProfileFixture(runId: String, spec: BackendSpec): File {
        check(sourceDirectory.isDirectory || sourceDirectory.mkdir()) { "cannot create run input directory" }
        val file = File(sourceDirectory, "$runId-profile-v3.json")
        check(file.createNewFile()) { "run profile fixture already exists" }
        val bytes = (
            "{\"backend_key\":\"" + spec.key + "\",\"schema_version\":3}\n"
            ).toByteArray(Charsets.UTF_8)
        file.outputStream().use { output ->
            output.write(bytes)
            output.flush()
            output.fd.sync()
        }
        check(file.setReadable(true, true) && file.setWritable(true, true)) {
            "cannot restrict run profile fixture"
        }
        return file
    }

    private companion object {
        const val PROFILE_ARTIFACT_ID: String = "profile"
    }
}
