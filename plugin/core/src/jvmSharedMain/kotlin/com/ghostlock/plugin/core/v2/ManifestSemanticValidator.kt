package com.ghostlock.plugin.core.v2

internal object ManifestSemanticValidator {
    fun validate(manifest: PluginPackageManifestV2) {
        require(manifest.format == ContractVersionModel(2, 0)) { "manifest format must be 2.0" }
        validateApi(manifest.api.runtime, "runtime")
        validateApi(manifest.api.ui, "ui")
        require(manifest.i18n.namespace == "${manifest.packageInfo.publisherId}.${manifest.packageInfo.packageId}") {
            "i18n namespace must equal publisherId.packageId"
        }

        val files = manifest.files.associateUnique(FileModel::path, "file path")
        require("manifest.json" !in files) { "manifest.json must not be declared in files" }
        validatePayloads(manifest, files)

        val resources = manifest.resources.associateUnique(ResourceModel::id, "resource id")
        manifest.resources.forEach { require(it.path in files) { "resource ${it.id} references missing file ${it.path}" } }
        manifest.i18n.bundles.associateUnique(I18nBundleModel::locale, "i18n locale").also { bundles ->
            require(manifest.i18n.defaultLocale in bundles) { "defaultLocale must have a bundle" }
        }
        manifest.i18n.bundles.forEach { require(it.path in files) { "i18n bundle references missing file ${it.path}" } }

        manifest.components.associateUnique({ it.common.id }, "component id")
        manifest.components.forEach { validateComponent(manifest, it, resources) }
        validatePayloadUse(manifest)
        val referencedResourceIds = manifest.components.flatMapTo(mutableSetOf()) { it.common.resources }
        require(resources.keys.all(referencedResourceIds::contains)) { "package resource is not owned by any component" }
        validateDataOnly(manifest, files)
        validateEveryFileHasDeclaredOwner(manifest, files)
    }

    private fun validateApi(api: ApiSideModel, label: String) {
        require(api.major == 2) { "$label API major must be 2" }
        require(api.minimumHostMinor <= api.builtAgainstMinor) {
            "$label minimumHostMinor must not exceed builtAgainstMinor"
        }
    }

    private fun validatePayloads(manifest: PluginPackageManifestV2, files: Map<String, FileModel>) {
        val payloads = manifest.payloads
        if (manifest.target.platform == PlatformModel.ANDROID && !manifest.target.dataOnly) {
            val expectedAbi = when (manifest.target.architecture) {
                ArchitectureModel.ARM64 -> "arm64-v8a"
                ArchitectureModel.ARMV7 -> "armeabi-v7a"
                ArchitectureModel.X86 -> "x86"
                ArchitectureModel.X86_64 -> "x86_64"
                ArchitectureModel.UNIVERSAL -> fail("Android executable package cannot target UNIVERSAL architecture")
            }
            require(manifest.target.abi == expectedAbi) { "Android target ABI does not match target architecture" }
        }
        payloads.ui?.code?.also { code ->
            code.unique("UI code path").forEach { require(it in files) { "UI payload references missing file $it" } }
            if (manifest.target.platform == PlatformModel.ANDROID) validateDexSequence(code, "UI")
        }
        payloads.runtime?.let { runtime ->
            runtime.code.unique("runtime code path").forEach { require(it in files) { "runtime payload references missing file $it" } }
            if (manifest.target.platform == PlatformModel.ANDROID) validateDexSequence(runtime.code, "runtime")
            runtime.nativeLibraries.associateUnique(NativeLibraryModel::path, "native library path")
            runtime.nativeLibraries.forEach { library ->
                require(library.path in files) { "native library references missing file ${library.path}" }
                require(library.abi == manifest.target.abi) { "native library ABI does not match target ABI" }
                val expectedClass = when (manifest.target.architecture) {
                    ArchitectureModel.ARM64, ArchitectureModel.X86_64 -> 64
                    ArchitectureModel.ARMV7, ArchitectureModel.X86 -> 32
                    ArchitectureModel.UNIVERSAL -> fail("executable runtime cannot target UNIVERSAL architecture")
                }
                require(library.elfClass == expectedClass) { "native library ELF class does not match target architecture" }
                val expectedMachine = when (manifest.target.architecture) {
                    ArchitectureModel.ARM64 -> 183
                    ArchitectureModel.ARMV7 -> 40
                    ArchitectureModel.X86 -> 3
                    ArchitectureModel.X86_64 -> 62
                    ArchitectureModel.UNIVERSAL -> fail("native library cannot target UNIVERSAL architecture")
                }
                require(library.elfMachine == expectedMachine) { "native library ELF machine does not match target architecture" }
            }
        }
        val declaredNative = payloads.runtime?.nativeLibraries?.mapTo(mutableSetOf(), NativeLibraryModel::path).orEmpty()
        val uiCode = payloads.ui?.code?.toSet().orEmpty()
        require(uiCode.intersect(declaredNative).isEmpty()) { "UI payload cannot reference runtime native libraries" }
        val runtimeCode = payloads.runtime?.code?.toSet().orEmpty()
        require(runtimeCode.intersect(declaredNative).isEmpty()) { "runtime native libraries cannot also be code entries" }
        files.keys.filter { it.endsWith(".so") }.forEach {
            require(it in declaredNative) { "shared library $it is not declared by runtime payload" }
        }
    }

    private fun validateDexSequence(code: List<String>, label: String) {
        val ordinals = code.map { path ->
            val name = path.substringAfterLast('/')
            val match = DEX.matchEntire(name) ?: fail("$label Android code entry is not classes*.dex: $path")
            match.groupValues[1].takeIf(String::isNotEmpty)?.toIntOrNull() ?: 1
        }
        require(ordinals == (1..ordinals.size).toList()) { "$label DEX entries must be ordered and contiguous" }
    }

    private fun validateComponent(
        manifest: PluginPackageManifestV2,
        component: ComponentModel,
        resources: Map<String, ResourceModel>,
    ) {
        val common = component.common
        common.resources.forEach { require(it in resources) { "component ${common.id} references missing resource $it" } }
        common.dependencies.associateUnique(DependencyModel::slotId, "dependency slot")
        common.exports.associateUnique(CapabilityExportModel::id, "capability export id")
        common.hostOperations.associateUnique(HostOperationModel::slotId, "Host operation slot")
        common.exports.forEach { export ->
            export.operations.associateUnique(OperationModel::id, "capability operation id")
        }
        common.uiEntryClass?.let { require(manifest.payloads.ui != null) { "UI entry requires UI payload" } }
        val needsRuntime = common.runtimeEntryClass != null || common.exports.isNotEmpty() || when (component) {
            is ProviderComponentModel -> component.jobs.isNotEmpty()
            is ToolComponentModel -> component.jobs.isNotEmpty()
            else -> false
        }
        if (needsRuntime) require(manifest.payloads.runtime != null) { "runtime entry/job/export requires runtime payload" }

        when (component) {
            is BackendComponentModel -> {
                component.providerSlots.associateUnique(ProviderSlotModel::id, "provider slot")
            }
            is ProviderComponentModel -> {
                require(
                    component.common.runtimeEntryClass != null ||
                        component.jobs.isNotEmpty() || component.staticExports.isNotEmpty(),
                ) {
                    "provider must declare a runtime entry, job, or static export"
                }
                validateJobs(component.jobs)
                val exports = validateStaticExports(component.staticExports, resources, common.resources.toSet())
                component.providerBinding.physicalStaticExportId?.let { id ->
                    val export = exports[id] ?: fail("physicalStaticExportId does not name a static export")
                    require(export.contractId == component.providerBinding.contractId && export.contractVersion == component.providerBinding.contractVersion) {
                        "physical static export contract must match provider binding"
                    }
                }
            }
            is ToolComponentModel -> {
                require(component.jobs.isNotEmpty() || component.staticExports.isNotEmpty()) {
                    "tool must declare a job or static export"
                }
                validateJobs(component.jobs)
                validateStaticExports(component.staticExports, resources, common.resources.toSet())
            }
            is CustomExecutorComponentModel -> Unit
        }
    }

    private fun validatePayloadUse(manifest: PluginPackageManifestV2) {
        val uiRequired = manifest.components.any { it.common.uiEntryClass != null }
        val runtimeRequired = manifest.components.any { component ->
            component.common.runtimeEntryClass != null || component.common.exports.isNotEmpty() || when (component) {
                is ProviderComponentModel -> component.jobs.isNotEmpty()
                is ToolComponentModel -> component.jobs.isNotEmpty()
                else -> false
            }
        }
        require((manifest.payloads.ui != null) == uiRequired) { "UI payload presence must match declared UI entry classes" }
        require((manifest.payloads.runtime != null) == runtimeRequired) {
            "runtime payload presence must match declared runtime entries, jobs, or capability exports"
        }
    }

    private fun validateJobs(jobs: List<JobModel>) {
        jobs.associateUnique(JobModel::id, "job id")
        jobs.forEach { job ->
            job.inputs.associateUnique(IoDeclarationModel::id, "job input id")
            job.outputs.associateUnique(IoDeclarationModel::id, "job output id")
        }
    }

    private fun validateStaticExports(
        staticExports: List<StaticExportModel>,
        resources: Map<String, ResourceModel>,
        ownedResourceIds: Set<String>,
    ): Map<String, StaticExportModel> = staticExports.associateUnique(StaticExportModel::id, "static export id").also {
        staticExports.forEach { export ->
            export.outputs.associateUnique(StaticOutputModel::outputId, "static output id")
            export.outputs.forEach { output ->
                val resource = resources[output.resourceId] ?: fail("static output references missing resource ${output.resourceId}")
                require(output.resourceId in ownedResourceIds) {
                    "static output must reference a resource owned by its component"
                }
                require(resource is DocumentResourceModel || resource is ArtifactResourceModel) {
                    "static output must reference a Document or Artifact resource"
                }
            }
        }
    }

    private fun validateDataOnly(manifest: PluginPackageManifestV2, files: Map<String, FileModel>) {
        if (!manifest.target.dataOnly) {
            require(manifest.target.platform != PlatformModel.UNIVERSAL && manifest.target.architecture != ArchitectureModel.UNIVERSAL) {
                "executable package must use a concrete platform and architecture"
            }
            val hasActiveContent = manifest.payloads.ui != null || manifest.payloads.runtime != null ||
                manifest.resources.any(ResourceModel::executable) || manifest.components.any { component ->
                    component.common.uiEntryClass != null || component.common.runtimeEntryClass != null ||
                        component.common.exports.isNotEmpty() || when (component) {
                            is ProviderComponentModel -> component.jobs.isNotEmpty()
                            is ToolComponentModel -> component.jobs.isNotEmpty()
                            is BackendComponentModel, is CustomExecutorComponentModel -> true
                        }
                }
            require(hasActiveContent) { "package without active content must be declared dataOnly" }
            return
        }
        require(manifest.target.platform == PlatformModel.UNIVERSAL && manifest.target.architecture == ArchitectureModel.UNIVERSAL) {
            "data-only package must use UNIVERSAL platform and architecture"
        }
        require(manifest.target.abi == "universal") { "data-only package ABI must be universal" }
        require(manifest.payloads.ui == null && manifest.payloads.runtime == null) { "data-only package cannot contain payloads" }
        require(manifest.resources.none(ResourceModel::executable)) { "data-only package cannot contain executable resources" }
        manifest.components.forEach { component ->
            val common = component.common
            require(common.uiEntryClass == null && common.runtimeEntryClass == null && common.exports.isEmpty()) {
                "data-only component cannot declare entry classes or capability exports"
            }
            when (component) {
                is BackendComponentModel, is CustomExecutorComponentModel -> fail("data-only package cannot contain backend or Custom Executor")
                is ProviderComponentModel -> {
                    require(component.jobs.isEmpty()) { "data-only provider cannot declare jobs" }
                    require(component.providerBinding.physicalStaticExportId != null) {
                        "data-only provider requires physicalStaticExportId"
                    }
                }
                is ToolComponentModel -> require(component.jobs.isEmpty()) { "data-only tool cannot declare jobs" }
            }
        }
        val passivePaths = buildSet {
            manifest.resources.mapTo(this, ResourceModel::path)
            manifest.i18n.bundles.mapTo(this, I18nBundleModel::path)
        }
        require(files.keys.all(passivePaths::contains)) { "data-only package has undeclared executable or passive files" }
    }

    private fun validateEveryFileHasDeclaredOwner(
        manifest: PluginPackageManifestV2,
        files: Map<String, FileModel>,
    ) {
        val declaredPaths = buildSet {
            manifest.payloads.ui?.code?.let(::addAll)
            manifest.payloads.runtime?.code?.let(::addAll)
            manifest.payloads.runtime?.nativeLibraries?.mapTo(this, NativeLibraryModel::path)
            manifest.resources.mapTo(this, ResourceModel::path)
            manifest.i18n.bundles.mapTo(this, I18nBundleModel::path)
        }
        require(files.keys.all(declaredPaths::contains)) { "files contains an entry with no declared payload/resource/i18n owner" }
    }

    private fun <T, K> Iterable<T>.associateUnique(key: (T) -> K, label: String): Map<K, T> {
        val result = linkedMapOf<K, T>()
        forEach { value -> require(result.put(key(value), value) == null) { "duplicate $label: ${key(value)}" } }
        return result
    }

    private fun <T> List<T>.unique(label: String): List<T> = also { require(size == toSet().size) { "duplicate $label" } }
    private fun fail(message: String): Nothing = throw IllegalArgumentException(message)
    private val DEX = Regex("classes(?:([2-9][0-9]*))?\\.dex")
}
