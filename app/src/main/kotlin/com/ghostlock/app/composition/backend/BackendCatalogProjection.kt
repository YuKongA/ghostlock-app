package com.ghostlock.app.composition.backend

import com.ghostlock.app.domain.backend.AbortPolicy
import com.ghostlock.app.domain.backend.BackendAvailability
import com.ghostlock.app.domain.backend.BackendAvailabilityReason
import com.ghostlock.app.domain.backend.BackendDescriptor
import com.ghostlock.app.domain.backend.BackendSelectionPolicy
import com.ghostlock.app.domain.backend.ExecutionDomain
import com.ghostlock.app.domain.run.RunCapability
import com.ghostlock.plugin.core.PluginAbortPolicy
import com.ghostlock.plugin.core.PluginCatalogSnapshot
import com.ghostlock.plugin.core.PluginComponentIdentity
import com.ghostlock.plugin.core.PluginComponentKind
import com.ghostlock.plugin.core.PluginContentSha256
import com.ghostlock.plugin.core.PluginExecutionDomain
import com.ghostlock.plugin.core.PluginPackageIdentity
import com.ghostlock.plugin.core.PluginRunCapability
import com.ghostlock.plugin.core.PluginSelectionPolicy
import com.ghostlock.plugin.api.v1.PluginApi

/** Exact immutable object selected from the same catalog generation as its descriptor. */
data class PluginObjectRef(
    val packageIdentity: PluginPackageIdentity,
    val variant: String,
    val versionCode: Long,
    val pluginContentSha256: PluginContentSha256,
)

/** Host descriptor and exact package object projected from one authenticated catalog record. */
data class ResolvedBackendProjection(
    val descriptor: BackendDescriptor,
    val objectRef: PluginObjectRef,
    val availability: BackendAvailability = BackendAvailability.Ready(),
)

/** One terminal catalog generation used by UI lookup and run admission. */
class BackendCatalogView internal constructor(
    val revision: Long,
    projections: List<ResolvedBackendProjection>,
) {
    val projections: List<ResolvedBackendProjection> = projections.toList()
    val descriptors: List<BackendDescriptor> = this.projections.map(ResolvedBackendProjection::descriptor)
    private val byKey: Map<String, ResolvedBackendProjection> = this.projections.associateBy {
        it.descriptor.key
    }

    fun resolve(key: String?): ResolvedBackendProjection? = key?.let(byKey::get)
}

/** Pure catalog-to-Host projection; it cannot add package membership or runtime instances. */
object BackendCatalogProjection {
    fun project(
        snapshot: PluginCatalogSnapshot,
        sdkInt: Int,
        processIs64Bit: Boolean,
        machine: String,
    ): BackendCatalogView {
        val normalized = snapshot.normalized()
        val seenBackendKeys = mutableSetOf<String>()
        val seenComponents = mutableSetOf<PluginComponentIdentity>()
        val projections = buildList {
            normalized.activeRecords.filter { it.enabled }.forEach { record ->
                val manifest = record.manifest
                val packageIdentity = manifest.packageIdentity
                manifest.components.forEach { component ->
                    require(component.kind == PluginComponentKind.BACKEND) {
                        "catalog projection only accepts BACKEND components"
                    }
                    val componentIdentity = PluginComponentIdentity(packageIdentity, component.id)
                    require(seenComponents.add(componentIdentity)) {
                        "duplicate full component identity: $componentIdentity"
                    }
                    require(seenBackendKeys.add(component.backendKey)) {
                        "duplicate backend key: ${component.backendKey}"
                    }
                    add(
                        ResolvedBackendProjection(
                            descriptor = BackendDescriptor(
                                key = component.backendKey,
                                selectionPolicy = component.selectionPolicy.toHost(),
                                abortPolicy = component.abortPolicy.toHost(),
                                executionDomain = component.executionDomains.toHost(),
                                runCapability = component.runCapability.toHost(),
                            ),
                            objectRef = PluginObjectRef(
                                packageIdentity = packageIdentity,
                                variant = manifest.variant,
                                versionCode = manifest.versionCode,
                                pluginContentSha256 = record.pluginContentSha256,
                            ),
                            availability = compatibility(
                                manifest = manifest,
                                component = component,
                                sdkInt = sdkInt,
                                processIs64Bit = processIs64Bit,
                                machine = machine,
                            ),
                        ),
                    )
                }
            }
        }.sortedBy { it.descriptor.key }
        return BackendCatalogView(normalized.revision, projections)
    }

    private fun compatibility(
        manifest: com.ghostlock.plugin.core.PluginPackageManifest,
        component: com.ghostlock.plugin.core.PluginManifestComponent,
        sdkInt: Int,
        processIs64Bit: Boolean,
        machine: String,
    ): BackendAvailability {
        val reason = when {
            manifest.hostApiMajor != PluginApi.MAJOR || manifest.minHostApiMinor > PluginApi.MINOR ->
                BackendAvailabilityReason(
                    "plugin-api-incompatible",
                    "Plugin API is not compatible with this Host",
                    "package=${manifest.hostApiMajor}.${manifest.minHostApiMinor} " +
                        "host=${PluginApi.MAJOR}.${PluginApi.MINOR}",
                )
            manifest.minAndroidSdk > sdkInt -> BackendAvailabilityReason(
                "android-sdk-incompatible",
                "Plugin requires a newer Android version",
                "required=${manifest.minAndroidSdk} current=$sdkInt",
            )
            manifest.variant != ANDROID_ARM64_VARIANT || !processIs64Bit || machine != AARCH64_MACHINE ||
                manifest.nativeLibraries.any { it.abi != ARM64_ABI } -> BackendAvailabilityReason(
                "process-abi-incompatible",
                "Plugin does not support this process architecture",
                "variant=${manifest.variant} is64Bit=$processIs64Bit machine=$machine",
            )
            component.entryContract != DIAGNOSTIC_ENTRY_CONTRACT ||
                component.runCapability != PluginRunCapability.DIAGNOSTIC_ONLY -> BackendAvailabilityReason(
                "entry-contract-incompatible",
                "Plugin entry contract is not supported by this Host",
                "entryContract=${component.entryContract} capability=${component.runCapability}",
            )
            else -> null
        }
        return reason?.let { BackendAvailability.NotReady(listOf(it)) } ?: BackendAvailability.Ready()
    }

    private const val ANDROID_ARM64_VARIANT = "android-arm64-v8a"
    private const val ARM64_ABI = "arm64-v8a"
    private const val AARCH64_MACHINE = "aarch64"
    private const val DIAGNOSTIC_ENTRY_CONTRACT = "diagnostic-v1"
}

private fun PluginSelectionPolicy.toHost(): BackendSelectionPolicy = when (this) {
    PluginSelectionPolicy.SELECTABLE -> BackendSelectionPolicy.SELECTABLE
}

private fun PluginAbortPolicy.toHost(): AbortPolicy = when (this) {
    PluginAbortPolicy.BEFORE_START_ONLY -> AbortPolicy.BEFORE_START_ONLY
}

private fun List<PluginExecutionDomain>.toHost(): ExecutionDomain = when (toSet()) {
    setOf(PluginExecutionDomain.APP) -> ExecutionDomain.APP
    setOf(PluginExecutionDomain.SHIZUKU_SHELL) -> ExecutionDomain.SHIZUKU_SHELL
    setOf(PluginExecutionDomain.APP, PluginExecutionDomain.SHIZUKU_SHELL) -> ExecutionDomain.APP_OR_SHIZUKU
    else -> throw IllegalArgumentException("unsupported execution domain set: $this")
}

private fun PluginRunCapability.toHost(): RunCapability = when (this) {
    PluginRunCapability.DIAGNOSTIC_ONLY -> RunCapability.DIAGNOSTIC_ONLY
}
