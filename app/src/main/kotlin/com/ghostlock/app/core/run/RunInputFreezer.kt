package com.ghostlock.app.core.run

import android.content.Context
import com.ghostlock.app.data.run.RootManagerArtifactResolver
import com.ghostlock.app.domain.backend.BackendDescriptor
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
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Deferred

/**
 * Single application boundary that freezes backend/profile/Payload/Finish and opened source objects.
 *
 * @param context Application context used for the private immutable profile fixture directory.
 * @param payloadRepository Complete typed Payload authority.
 * @param finishRepository Complete typed Finish authority.
 * @param rootManagerResolver Installed manager executable and exact argv resolver.
 * @note Product carriers are supplied only by a concrete backend artifact design; no global carrier wrapper exists.
 */
class RunInputFreezer(
    context: Context,
    private val payloadRepository: PayloadRepository,
    private val finishRepository: FinishRepository,
    private val rootManagerResolver: RootManagerArtifactResolver,
) {
    private val lock = Any()
    private val sourceDirectory = File(context.applicationContext.noBackupFilesDir, "run-input")
    private var generation: Long = 0L

    /**
     * Queues Payload and Finish snapshots at the caller's run-action cutoff.
     *
     * @param spec Exact selected backend descriptor.
     * @return Handles whose immutable values cannot include later accepted edits.
     */
    fun cutoff(spec: BackendDescriptor): RunInputCutoff = when (spec.runCapability) {
        RunCapability.DIAGNOSTIC_ONLY -> RunInputCutoff(
            backendKey = spec.key,
            payload = CompletableDeferred(PayloadDefinition(items = emptyList())),
            finish = CompletableDeferred(FinishDefinition()),
        )
        RunCapability.PRODUCT_ROOT -> RunInputCutoff(
            backendKey = spec.key,
            payload = payloadRepository.snapshot(),
            finish = finishRepository.snapshot(),
        )
        RunCapability.UNRESOLVED -> error("cannot freeze unresolved backend capability")
    }

    /**
     * Freezes one run and returns ownership of every master descriptor in one closeable lease.
     *
     * @param spec Exact selected backend descriptor.
     * @return Immutable run input plus opaque-token descriptor ownership.
     * @throws IllegalStateException When an input or manager object cannot be fixed exactly once.
     */
    suspend fun freeze(spec: BackendDescriptor, cutoff: RunInputCutoff): FrozenRunInputLease {
        require(cutoff.backendKey == spec.key) { "run input cutoff backend mismatch" }
        val identity = nextIdentity()
        val profileFile = publishProfileFixture(identity.first, spec)
        val sources = mutableListOf<OpenedRunInputSource>()
        try {
            val payload = cutoff.payload.await()
            val finish = cutoff.finish.await()
            val profileSource = OpenedRunInputSource.open(
                file = profileFile,
                logicalId = PROFILE_ARTIFACT_ID,
                kind = RunInputArtifactKind.PROFILE,
                ownedSource = true,
            )
            sources += profileSource
            val invocations = mutableListOf<FrozenArtifactInvocation>()
            if (spec.runCapability == RunCapability.PRODUCT_ROOT) {
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
    private fun publishProfileFixture(runId: String, spec: BackendDescriptor): File {
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

/** Exact DataStore actor-cutoff handles captured synchronously by one run action. */
class RunInputCutoff internal constructor(
    val backendKey: String,
    val payload: Deferred<PayloadDefinition>,
    val finish: Deferred<FinishDefinition>,
)
