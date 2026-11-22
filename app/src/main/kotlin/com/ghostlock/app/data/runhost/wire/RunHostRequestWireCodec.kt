package com.ghostlock.app.data.runhost.wire

import android.os.ParcelFileDescriptor
import android.system.ErrnoException
import android.system.Os
import android.system.OsConstants
import com.ghostlock.app.core.runhost.RunHostArtifactKind
import com.ghostlock.app.core.runhost.RunHostArtifactRequest
import com.ghostlock.app.core.runhost.RunHostFinishItemRequest
import com.ghostlock.app.core.runhost.RunHostFinishKind
import com.ghostlock.app.core.runhost.RunHostFinishSeverity
import com.ghostlock.app.core.runhost.RunHostPayloadAcceptance
import com.ghostlock.app.core.runhost.RunHostPayloadItemRequest
import com.ghostlock.app.core.runhost.RunHostPayloadKind
import com.ghostlock.app.core.runhost.RunHostRequestModel
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.finish.FinishSeverity
import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.run.FrozenRunInput
import com.ghostlock.app.data.run.OpenedRunInputArtifact
import com.ghostlock.app.run.FinishItemParcel
import com.ghostlock.app.run.PayloadItemParcel
import com.ghostlock.app.run.RunHostRequest
import com.ghostlock.app.run.RunInputArtifactParcel

/** RunHost-owned descriptor batch whose untransferred remainder closes in reverse acquisition order. */
internal class OwnedRunHostRequestDescriptors(
    pluginArchive: ParcelFileDescriptor,
    artifacts: List<OpenedRunInputArtifact>,
) : AutoCloseable {
    private var pluginArchive: ParcelFileDescriptor? = pluginArchive
    private var artifacts: List<OpenedRunInputArtifact>? = artifacts.toList()

    /** Transfers the archive descriptor exactly once to the plugin loader. */
    @Synchronized
    fun takePluginArchive(): ParcelFileDescriptor =
        checkNotNull(pluginArchive) { "plugin archive descriptor was already transferred or closed" }
            .also { pluginArchive = null }

    /** Transfers the ordinary descriptors exactly once to the staging adapter. */
    @Synchronized
    fun takeArtifacts(): List<OpenedRunInputArtifact> =
        checkNotNull(artifacts) { "artifact descriptors were already transferred or closed" }
            .also { artifacts = null }

    /** Closes only descriptors that have not already moved to their single consumer. */
    @Synchronized
    override fun close() {
        val remainingArtifacts = artifacts
        artifacts = null
        remainingArtifacts.orEmpty().asReversed().forEach { artifact ->
            runCatching { artifact.descriptor.close() }
        }
        val remainingPlugin = pluginArchive
        pluginArchive = null
        runCatching { remainingPlugin?.close() }
    }
}

/**
 * Builds the generated typed request from one neutral frozen input.
 *
 * @param duplicateSource Callback that duplicates one opaque source token into caller-owned wire state.
 * @return AIDL request owning every successfully duplicated descriptor.
 * @throws Throwable After closing every descriptor duplicated before a mapping failure.
 */
internal fun FrozenRunInput.toWireRequest(
    duplicatePluginSource: (String) -> ParcelFileDescriptor,
    duplicateSource: (String) -> ParcelFileDescriptor,
): RunHostRequest {
    val invocationByItemId = artifactInvocations.associateBy { invocation -> invocation.itemId }
    var openedPlugin: ParcelFileDescriptor? = null
    val openedArtifacts = mutableListOf<RunInputArtifactParcel>()
    try {
        openedPlugin = duplicatePluginSource(pluginSourceToken)
        inputArtifacts.forEach { artifact ->
            openedArtifacts += RunInputArtifactParcel().also { parcel ->
                parcel.logicalId = artifact.logicalId
                parcel.kind = artifact.kind.wireValue
                parcel.byteCount = artifact.byteCount
                parcel.sha256 = artifact.sha256
                parcel.fd = duplicateSource(artifact.sourceToken)
            }
        }
        return RunHostRequest().also { request ->
            request.runId = runId
            request.generation = generation
            request.backendKey = backendKey
            request.pluginArchiveFd = openedPlugin
            request.pluginContentSha256 = pluginContentSha256
            request.profileArtifactId = profile.artifactId
            request.artifacts = openedArtifacts
            request.payloadItems = payload.enabledItems().map { item ->
                PayloadItemParcel().also { parcel ->
                    parcel.itemId = item.id
                    parcel.required = item.required
                    when (item.kind) {
                        PayloadKind.Command -> {
                            require(item.scriptMode == 0) {
                                "script-mode Payload requires a staged USER_SCRIPT artifact"
                            }
                            parcel.kind = 1
                            parcel.artifactId = null
                            parcel.args = item.command.trim().split(Regex("\\s+")).filter(String::isNotEmpty)
                        }

                        PayloadKind.RootManager -> {
                            val invocation = requireNotNull(invocationByItemId[item.id]) {
                                "root-manager artifact invocation is missing"
                            }
                            parcel.kind = 3
                            parcel.artifactId = invocation.artifactId
                            parcel.args = invocation.args
                        }

                        PayloadKind.Lkm -> error("LKM artifact staging belongs to a carrier child plan")
                    }
                    parcel.acceptanceKind = when (item.acceptance) {
                        PayloadAcceptance.ExitZero -> 1
                        PayloadAcceptance.DaemonAck -> 2
                        PayloadAcceptance.ModulePresent -> 3
                    }
                }
            }
            request.finishItems = finish.items.map { item ->
                FinishItemParcel().also { parcel ->
                    parcel.itemId = item.id
                    parcel.kind = when (item.kind) {
                        FinishKind.Command -> 1
                        FinishKind.Script -> error("Finish script staging belongs to the file artifact adapter")
                        FinishKind.Reboot -> 3
                    }
                    parcel.severity = when (item.severity) {
                        FinishSeverity.Normal -> 1
                        FinishSeverity.SafetyCritical -> 2
                    }
                    parcel.alwaysRun = item.alwaysRun
                    parcel.artifactId = null
                    parcel.args = item.args
                }
            }
        }
    } catch (problem: Throwable) {
        openedArtifacts.asReversed().forEach { artifact -> runCatching { artifact.fd.close() } }
        runCatching { openedPlugin?.close() }
        throw problem
    }
}

/**
 * Converts a generated AIDL request into the normalized in-process contract.
 *
 * @return Immutable request metadata; descriptor ownership remains with the Binder input until duplicated.
 * @throws IllegalArgumentException When a stable enum value is unknown.
 */
internal fun RunHostRequest.toModel(): RunHostRequestModel = RunHostRequestModel(
    runId = runId,
    generation = generation,
    backendKey = backendKey,
    pluginContentSha256 = pluginContentSha256,
    profileArtifactId = profileArtifactId,
    payloadItems = payloadItems.map(PayloadItemParcel::toModel),
    finishItems = finishItems.map(FinishItemParcel::toModel),
    artifacts = artifacts.map(RunInputArtifactParcel::toModel),
)

/**
 * Duplicates every request descriptor so the RunHost owns its lifetime independently of Binder input objects.
 *
 * @param model Decoded request metadata in the same artifact order as this wire request.
 * @return Fully owned descriptor list.
 * @throws IllegalArgumentException When the wire and decoded artifact counts differ or a descriptor is null.
 */
internal fun RunHostRequest.duplicateDescriptors(
    model: RunHostRequestModel,
): OwnedRunHostRequestDescriptors {
    require(artifacts.size == model.artifacts.size) { "artifact count changed during decode" }
    var plugin: ParcelFileDescriptor? = null
    val owned = mutableListOf<OpenedRunInputArtifact>()
    try {
        val pluginSource = requireNotNull(pluginArchiveFd) { "plugin archive descriptor is null" }
        pluginSource.requireReadOnlyRegularPluginArchive()
        plugin = ParcelFileDescriptor.dup(pluginSource.fileDescriptor)
        artifacts.zip(model.artifacts).forEach { (wire, metadata) ->
            val source = requireNotNull(wire.fd) { "artifact descriptor is null" }
            owned += OpenedRunInputArtifact(metadata, ParcelFileDescriptor.dup(source.fileDescriptor))
        }
        return OwnedRunHostRequestDescriptors(requireNotNull(plugin), owned)
    } catch (problem: Throwable) {
        owned.asReversed().forEach { artifact -> runCatching { artifact.descriptor.close() } }
        runCatching { plugin?.close() }
        throw problem
    }
}

/** Closes the Binder-decoded descriptors on every endpoint exit without touching validated duplicates. */
internal fun RunHostRequest.closeDecodedDescriptors() {
    artifacts.orEmpty().asReversed().forEach { artifact ->
        runCatching {
            val descriptor = artifact.fd
            artifact.fd = null
            descriptor?.close()
        }
    }
    runCatching {
        val archive = pluginArchiveFd
        pluginArchiveFd = null
        archive?.close()
    }
}

/** Validates archive PFD facts that must fail before any RunHost workspace is created. */
private fun ParcelFileDescriptor.requireReadOnlyRegularPluginArchive() {
    try {
        val flags = Os.fcntlInt(fileDescriptor, OsConstants.F_GETFL, 0)
        val stat = Os.fstat(fileDescriptor)
        requireReadOnlyRegularPluginArchiveFacts(flags, stat.st_mode)
    } catch (problem: IllegalArgumentException) {
        throw problem
    } catch (problem: ErrnoException) {
        throw IllegalArgumentException("plugin archive descriptor cannot be inspected", problem)
    }
}

/** Pure kernel-fact assertion shared by the live PFD preflight and host-side unit coverage. */
internal fun requireReadOnlyRegularPluginArchiveFacts(flags: Int, mode: Int) {
    require(flags and OsConstants.O_ACCMODE == OsConstants.O_RDONLY) {
        "plugin archive descriptor is writable"
    }
    require(flags and LINUX_O_PATH == 0) { "plugin archive descriptor is O_PATH" }
    require(mode and OsConstants.S_IFMT == OsConstants.S_IFREG) {
        "plugin archive descriptor is not a regular file"
    }
}

/** Decodes one artifact metadata record and rejects unknown enum values. */
private fun RunInputArtifactParcel.toModel(): RunHostArtifactRequest = RunHostArtifactRequest(
    logicalId = logicalId,
    kind = RunHostArtifactKind.entries.firstOrNull { candidate -> candidate.wireValue == kind }
        ?: throw IllegalArgumentException("unknown artifact kind"),
    byteCount = byteCount,
    sha256 = sha256,
)

// Stable Linux UAPI flag absent from some Android SDK OsConstants stubs.
private const val LINUX_O_PATH = 0x200000

/** Decodes one Payload item and rejects unknown kind or acceptance values. */
private fun PayloadItemParcel.toModel(): RunHostPayloadItemRequest = RunHostPayloadItemRequest(
    itemId = itemId,
    kind = RunHostPayloadKind.entries.firstOrNull { candidate -> candidate.wireValue == kind }
        ?: throw IllegalArgumentException("unknown Payload kind"),
    required = required,
    artifactId = artifactId,
    args = args.toList(),
    acceptance = RunHostPayloadAcceptance.entries.firstOrNull { candidate ->
        candidate.wireValue == acceptanceKind
    } ?: throw IllegalArgumentException("unknown Payload acceptance"),
)

/** Decodes one Finish item and rejects unknown kind or severity values. */
private fun FinishItemParcel.toModel(): RunHostFinishItemRequest = RunHostFinishItemRequest(
    itemId = itemId,
    kind = RunHostFinishKind.entries.firstOrNull { candidate -> candidate.wireValue == kind }
        ?: throw IllegalArgumentException("unknown Finish kind"),
    severity = RunHostFinishSeverity.entries.firstOrNull { candidate -> candidate.wireValue == severity }
        ?: throw IllegalArgumentException("unknown Finish severity"),
    alwaysRun = alwaysRun,
    artifactId = artifactId,
    args = args.toList(),
)
