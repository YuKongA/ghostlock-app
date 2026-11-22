package com.ghostlock.plugin.core.v2

import kotlinx.serialization.json.JsonObject

data class PluginPackageManifestV2(
    val format: ContractVersionModel,
    val packageInfo: PackageInfoModel,
    val target: TargetModel,
    val api: ApiCompatibilityModel,
    val files: List<FileModel>,
    val payloads: PayloadsModel,
    val resources: List<ResourceModel>,
    val i18n: I18nModel,
    val components: List<ComponentModel>,
    internal val source: JsonObject,
)

data class ExactPackageObjectModel(
    val packageInfo: PackageInfoModel,
    val target: TargetModel,
    val semanticContentRoot: String,
)

data class ExactComponentObjectModel(
    val exactPackage: ExactPackageObjectModel,
    val componentId: String,
    val kind: ComponentKindModel,
)

data class ContractVersionModel(val major: Int, val minor: Int)

data class ContractVersionRangeModel(val major: Int, val minMinor: Int, val maxMinor: Int)

data class PackageInfoModel(
    val publisherId: String,
    val packageId: String,
    val versionCode: Long,
    val versionName: String,
    val variant: String,
)

enum class PlatformModel { ANDROID, DESKTOP_LINUX, DESKTOP_MACOS, DESKTOP_WINDOWS, UNIVERSAL }

enum class ArchitectureModel { ARM64, ARMV7, X86, X86_64, UNIVERSAL }

data class TargetModel(
    val platform: PlatformModel,
    val architecture: ArchitectureModel,
    val abi: String,
    val minimumPlatformVersion: Int,
    val dataOnly: Boolean,
)

data class ApiSideModel(
    val major: Int,
    val builtAgainstMinor: Int,
    val minimumHostMinor: Int,
)

data class ApiCompatibilityModel(val runtime: ApiSideModel, val ui: ApiSideModel)

data class FileModel(val path: String, val sha256: String, val size: Long)

data class PayloadsModel(val ui: CodePayloadModel?, val runtime: RuntimePayloadModel?)

data class CodePayloadModel(val code: List<String>)

data class RuntimePayloadModel(
    val code: List<String>,
    val nativeLibraries: List<NativeLibraryModel>,
)

data class NativeLibraryModel(
    val path: String,
    val abi: String,
    val elfClass: Int,
    val elfMachine: Int,
    val soname: String,
    val neededLibraries: List<String>,
)

sealed interface ResourceModel {
    val id: String
    val path: String
    val executable: Boolean
}

data class DocumentResourceModel(
    override val id: String,
    override val path: String,
    val schemaId: String,
    val schemaVersion: ContractVersionModel,
) : ResourceModel {
    override val executable: Boolean = false
}

data class ArtifactResourceModel(
    override val id: String,
    override val path: String,
    val artifactKind: String,
    val target: TargetModel,
) : ResourceModel {
    override val executable: Boolean = false
}

data class WebBundleResourceModel(
    override val id: String,
    override val path: String,
    val entryPath: String,
) : ResourceModel {
    override val executable: Boolean = true
}

data class SourceResourceModel(
    override val id: String,
    override val path: String,
    val sourceKindId: String,
) : ResourceModel {
    override val executable: Boolean = false
}

enum class PassiveResourceKind { NOTICE, PASSIVE_DATA }

data class PassiveResourceModel(
    override val id: String,
    override val path: String,
    val kind: PassiveResourceKind,
) : ResourceModel {
    override val executable: Boolean = false
}

data class I18nModel(
    val namespace: String,
    val defaultLocale: String,
    val bundles: List<I18nBundleModel>,
)

data class I18nBundleModel(val locale: String, val path: String)

data class StableComponentSelectorModel(
    val publisherId: String,
    val packageId: String,
    val componentId: String,
)

enum class DependencyModeModel { CAPABILITY, STATIC }

data class DependencyModel(
    val slotId: String,
    val required: Boolean,
    val mode: DependencyModeModel,
    val target: StableComponentSelectorModel,
    val exportId: String,
    val contractId: String,
    val contractVersion: ContractVersionRangeModel,
)

enum class CapabilityVisibilityModel { DEPENDENCY, CUSTOM_CALLABLE }

data class OperationModel(
    val id: String,
    val requestSchema: String,
    val responseSchema: String,
)

data class CapabilityExportModel(
    val id: String,
    val contractId: String,
    val contractVersion: ContractVersionModel,
    val visibility: Set<CapabilityVisibilityModel>,
    val entryClass: String,
    val operations: List<OperationModel>,
)

data class StaticOutputModel(val outputId: String, val resourceId: String)

data class StaticExportModel(
    val id: String,
    val contractId: String,
    val contractVersion: ContractVersionModel,
    val outputs: List<StaticOutputModel>,
)

data class HostOperationModel(
    val slotId: String,
    val contractId: String,
    val contractVersion: ContractVersionRangeModel,
)

enum class IoKindModel { DOCUMENT, ARTIFACT }

sealed interface IoDeclarationModel {
    val id: String
    val kind: IoKindModel
}

data class DocumentIoModel(
    override val id: String,
    val schemaId: String,
    val schemaVersion: ContractVersionModel,
) : IoDeclarationModel {
    override val kind: IoKindModel = IoKindModel.DOCUMENT
}

data class ArtifactIoModel(
    override val id: String,
    val artifactKind: String,
    val target: TargetModel,
) : IoDeclarationModel {
    override val kind: IoKindModel = IoKindModel.ARTIFACT
}

data class JobModel(
    val id: String,
    val entryClass: String,
    val inputs: List<IoDeclarationModel>,
    val outputs: List<IoDeclarationModel>,
)

data class ProviderSlotModel(
    val id: String,
    val required: Boolean,
    val contractId: String,
    val contractVersion: ContractVersionRangeModel,
)

data class ProviderBindingModel(
    val parent: StableComponentSelectorModel,
    val slotId: String,
    val contractId: String,
    val contractVersion: ContractVersionModel,
    val physicalStaticExportId: String?,
)

enum class ComponentKindModel { BACKEND, BACKEND_PROVIDER, TOOL, CUSTOM_EXECUTOR }

data class ComponentCommonModel(
    val id: String,
    val uiEntryClass: String?,
    val runtimeEntryClass: String?,
    val resources: List<String>,
    val dependencies: List<DependencyModel>,
    val exports: List<CapabilityExportModel>,
    val hostOperations: List<HostOperationModel>,
)

sealed interface ComponentModel {
    val kind: ComponentKindModel
    val common: ComponentCommonModel
}

enum class BackendSelectionPolicyModel { SELECTABLE, HIDDEN }

enum class ExecutionDomainModel { APP, SHIZUKU_SHELL }

enum class AbortPolicyModel { BEFORE_EXECUTE_ONLY, COOPERATIVE }

data class BackendComponentModel(
    override val common: ComponentCommonModel,
    val backendKey: String,
    val selectionPolicy: BackendSelectionPolicyModel,
    val executionDomains: Set<ExecutionDomainModel>,
    val abortPolicy: AbortPolicyModel,
    val providerSlots: List<ProviderSlotModel>,
) : ComponentModel {
    override val kind: ComponentKindModel = ComponentKindModel.BACKEND
}

data class ProviderComponentModel(
    override val common: ComponentCommonModel,
    val staticExports: List<StaticExportModel>,
    val providerBinding: ProviderBindingModel,
    val jobs: List<JobModel>,
) : ComponentModel {
    override val kind: ComponentKindModel = ComponentKindModel.BACKEND_PROVIDER
}

data class ToolComponentModel(
    override val common: ComponentCommonModel,
    val staticExports: List<StaticExportModel>,
    val jobs: List<JobModel>,
) : ComponentModel {
    override val kind: ComponentKindModel = ComponentKindModel.TOOL
}

data class CustomExecutorComponentModel(
    override val common: ComponentCommonModel,
    val languages: Set<String>,
    val webUiSupport: Boolean,
) : ComponentModel {
    override val kind: ComponentKindModel = ComponentKindModel.CUSTOM_EXECUTOR
}
