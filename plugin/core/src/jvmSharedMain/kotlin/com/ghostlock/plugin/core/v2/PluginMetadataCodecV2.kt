package com.ghostlock.plugin.core.v2

import java.security.MessageDigest
import java.text.Normalizer
import java.util.Locale
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.longOrNull

object PluginMetadataCodecV2 {
    private const val CONTENT_DOMAIN = "GhostLockPluginContent/2"

    private val json = Json {
        ignoreUnknownKeys = false
        isLenient = false
        allowSpecialFloatingPointValues = false
        explicitNulls = true
    }

    fun decodeManifest(source: String): PluginPackageManifestV2 {
        V2JsonScanner(source).scan()
        val root = try {
            json.parseToJsonElement(source).jsonObject
        } catch (error: IllegalArgumentException) {
            throw error
        } catch (error: Exception) {
            throw IllegalArgumentException("invalid plugin v2 manifest JSON", error)
        }
        return ManifestParser.parse(root).also(ManifestSemanticValidator::validate)
    }

    fun canonicalManifest(manifest: PluginPackageManifestV2): String =
        JcsCanonicalizer.canonicalize(manifest.source)

    fun semanticContentRoot(manifest: PluginPackageManifestV2): String {
        val domain = CONTENT_DOMAIN.encodeToByteArray()
        val canonical = canonicalManifest(manifest).encodeToByteArray()
        return sha256(ByteArray(domain.size + 1 + canonical.size).also { bytes ->
            domain.copyInto(bytes)
            bytes[domain.size] = 0
            canonical.copyInto(bytes, domain.size + 1)
        }).toLowerHex()
    }

    fun exactPackageObject(manifest: PluginPackageManifestV2): ExactPackageObjectModel =
        ExactPackageObjectModel(manifest.packageInfo, manifest.target, semanticContentRoot(manifest))

    fun exactComponentObjects(manifest: PluginPackageManifestV2): List<ExactComponentObjectModel> {
        val exactPackage = exactPackageObject(manifest)
        return manifest.components.map { component ->
            ExactComponentObjectModel(exactPackage, component.common.id, component.kind)
        }
    }

    internal fun canonicalizeJson(source: String): String {
        V2JsonScanner(source).scan()
        return JcsCanonicalizer.canonicalize(json.parseToJsonElement(source))
    }

    internal fun semanticRootOfJson(source: String): String {
        val domain = CONTENT_DOMAIN.encodeToByteArray()
        val canonical = canonicalizeJson(source).encodeToByteArray()
        return sha256(ByteArray(domain.size + 1 + canonical.size).also { bytes ->
            domain.copyInto(bytes)
            bytes[domain.size] = 0
            canonical.copyInto(bytes, domain.size + 1)
        }).toLowerHex()
    }
}

private object ManifestParser {
    fun parse(root: JsonObject): PluginPackageManifestV2 {
        val reader = root.reader(
            "manifest",
            required = setOf("format", "package", "target", "api", "files", "payloads", "resources", "i18n", "components"),
        )
        return PluginPackageManifestV2(
            format = version(reader.obj("format"), "format"),
            packageInfo = packageInfo(reader.obj("package")),
            target = target(reader.obj("target"), "target"),
            api = api(reader.obj("api")),
            files = reader.array("files").objects("files").map(::file).notEmpty("files"),
            payloads = payloads(reader.obj("payloads")),
            resources = reader.array("resources").objects("resources").map(::resource),
            i18n = i18n(reader.obj("i18n")),
            components = reader.array("components").objects("components").map(::component).notEmpty("components"),
            source = root,
        )
    }

    private fun version(value: JsonObject, label: String): ContractVersionModel {
        val r = value.reader(label, setOf("major", "minor"))
        return ContractVersionModel(r.int("major", minimum = 1), r.int("minor"))
    }

    private fun versionRange(value: JsonObject, label: String): ContractVersionRangeModel {
        val r = value.reader(label, setOf("major", "minMinor", "maxMinor"))
        return ContractVersionRangeModel(
            r.int("major", minimum = 1),
            r.int("minMinor"),
            r.int("maxMinor"),
        ).also { require(it.minMinor <= it.maxMinor) { "$label minMinor must not exceed maxMinor" } }
    }

    private fun packageInfo(value: JsonObject): PackageInfoModel {
        val r = value.reader("package", setOf("publisherId", "packageId", "versionCode", "versionName", "variant"))
        return PackageInfoModel(
            r.id("publisherId"),
            r.id("packageId"),
            r.long("versionCode", minimum = 1),
            r.string("versionName", maximumLength = 128).also { require(it.isNotEmpty()) },
            r.id("variant"),
        )
    }

    private fun target(value: JsonObject, label: String): TargetModel {
        val r = value.reader(label, setOf("platform", "architecture", "abi", "minimumPlatformVersion", "dataOnly"))
        return TargetModel(
            r.enum("platform"),
            r.enum("architecture"),
            r.string("abi", maximumLength = 64).also { require(ABI.matches(it)) { "$label.abi is invalid" } },
            r.int("minimumPlatformVersion"),
            r.boolean("dataOnly"),
        )
    }

    private fun api(value: JsonObject): ApiCompatibilityModel {
        val r = value.reader("api", setOf("runtime", "ui"))
        return ApiCompatibilityModel(apiSide(r.obj("runtime"), "api.runtime"), apiSide(r.obj("ui"), "api.ui"))
    }

    private fun apiSide(value: JsonObject, label: String): ApiSideModel {
        val r = value.reader(label, setOf("major", "builtAgainstMinor", "minimumHostMinor"))
        return ApiSideModel(r.int("major", minimum = 1), r.int("builtAgainstMinor"), r.int("minimumHostMinor"))
    }

    private fun file(value: JsonObject): FileModel {
        val r = value.reader("file", setOf("path", "sha256", "size"))
        return FileModel(r.path("path"), r.sha256("sha256"), r.long("size"))
    }

    private fun payloads(value: JsonObject): PayloadsModel {
        val r = value.reader("payloads", required = emptySet(), optional = setOf("ui", "runtime"))
        return PayloadsModel(
            r.optionalObj("ui")?.let { payload ->
                val p = payload.reader("payloads.ui", setOf("code"))
                CodePayloadModel(p.array("code").strings("payloads.ui.code").map(::requirePath).notEmpty("payloads.ui.code"))
            },
            r.optionalObj("runtime")?.let { payload ->
                val p = payload.reader("payloads.runtime", setOf("code", "nativeLibraries"))
                RuntimePayloadModel(
                    p.array("code").strings("payloads.runtime.code").map(::requirePath).notEmpty("payloads.runtime.code"),
                    p.array("nativeLibraries").objects("payloads.runtime.nativeLibraries").map(::nativeLibrary),
                )
            },
        )
    }

    private fun nativeLibrary(value: JsonObject): NativeLibraryModel {
        val r = value.reader("nativeLibrary", setOf("path", "abi", "elfClass", "elfMachine", "soname", "neededLibraries"))
        val elfClass = r.int("elfClass")
        require(elfClass == 32 || elfClass == 64) { "nativeLibrary.elfClass must be 32 or 64" }
        return NativeLibraryModel(
            r.path("path"), r.string("abi", maximumLength = 64).also { require(it.isNotEmpty()) },
            elfClass, r.int("elfMachine", maximum = 65535),
            r.string("soname", maximumLength = 255).also { require(it.isNotEmpty()) },
            r.array("neededLibraries").strings("neededLibraries").onEach { require(it.isNotEmpty() && it.length <= 255) }.unique("needed library"),
        )
    }

    private fun resource(value: JsonObject): ResourceModel = when (value.requiredString("kind")) {
        "DOCUMENT" -> value.reader("documentResource", setOf("id", "kind", "path", "schemaId", "schemaVersion", "executable")).let { r ->
            r.constant("kind", "DOCUMENT"); r.constant("executable", false)
            DocumentResourceModel(r.id("id"), r.path("path"), r.id("schemaId"), version(r.obj("schemaVersion"), "schemaVersion"))
        }
        "ARTIFACT" -> value.reader("artifactResource", setOf("id", "kind", "path", "artifactKind", "target", "executable")).let { r ->
            r.constant("kind", "ARTIFACT"); r.constant("executable", false)
            ArtifactResourceModel(r.id("id"), r.path("path"), r.id("artifactKind"), target(r.obj("target"), "resource.target"))
        }
        "WEB_BUNDLE" -> value.reader("webBundleResource", setOf("id", "kind", "path", "entryPath", "executable")).let { r ->
            r.constant("kind", "WEB_BUNDLE"); r.constant("executable", true)
            WebBundleResourceModel(r.id("id"), r.path("path"), r.path("entryPath"))
        }
        "SOURCE" -> value.reader("sourceResource", setOf("id", "kind", "path", "sourceKindId", "executable")).let { r ->
            r.constant("kind", "SOURCE"); r.constant("executable", false)
            SourceResourceModel(r.id("id"), r.path("path"), r.id("sourceKindId"))
        }
        "NOTICE", "PASSIVE_DATA" -> value.reader("passiveResource", setOf("id", "kind", "path", "executable")).let { r ->
            val kind = r.enum<PassiveResourceKind>("kind"); r.constant("executable", false)
            PassiveResourceModel(r.id("id"), r.path("path"), kind)
        }
        else -> throw IllegalArgumentException("resource.kind is not supported")
    }

    private fun i18n(value: JsonObject): I18nModel {
        val r = value.reader("i18n", setOf("namespace", "defaultLocale", "bundles"))
        return I18nModel(r.id("namespace"), requireLocale(r.string("defaultLocale")), r.array("bundles").objects("i18n.bundles").map { bundle ->
            val b = bundle.reader("i18n.bundle", setOf("locale", "path"))
            I18nBundleModel(requireLocale(b.string("locale")), b.path("path"))
        }.notEmpty("i18n.bundles"))
    }

    private fun component(value: JsonObject): ComponentModel {
        val kind = enumValue<ComponentKindModel>(value.requiredString("kind"), "component.kind")
        val commonFields = setOf("kind", "id", "uiEntryClass", "runtimeEntryClass", "resources", "dependencies", "exports", "hostOperations")
        val requiredCommon = setOf("kind", "id", "resources", "dependencies", "exports", "hostOperations")
        val extraRequired = when (kind) {
            ComponentKindModel.BACKEND -> setOf("uiEntryClass", "runtimeEntryClass", "backendKey", "selectionPolicy", "executionDomains", "abortPolicy", "providerSlots")
            ComponentKindModel.BACKEND_PROVIDER -> setOf("staticExports", "providerBinding", "jobs")
            ComponentKindModel.TOOL -> setOf("staticExports", "jobs")
            ComponentKindModel.CUSTOM_EXECUTOR -> setOf("uiEntryClass", "runtimeEntryClass", "languages", "webUiSupport")
        }
        val r = value.reader("component.${kind.name}", requiredCommon + extraRequired, commonFields + extraRequired)
        r.constant("kind", kind.name)
        val common = ComponentCommonModel(
            r.id("id"), r.optionalString("uiEntryClass")?.let(::requireClassName),
            r.optionalString("runtimeEntryClass")?.let(::requireClassName),
            r.array("resources").strings("component.resources").map(::requireId).unique("component resource"),
            r.array("dependencies").objects("component.dependencies").map(::dependency),
            r.array("exports").objects("component.exports").map(::capabilityExport),
            r.array("hostOperations").objects("component.hostOperations").map(::hostOperation),
        )
        return when (kind) {
            ComponentKindModel.BACKEND -> BackendComponentModel(
                common, r.id("backendKey"), r.enum("selectionPolicy"),
                r.array("executionDomains").strings("executionDomains").map { enumValue<ExecutionDomainModel>(it, "executionDomain") }.unique("execution domain").toSet().also {
                    require(it.isNotEmpty()) { "executionDomains must not be empty" }
                },
                r.enum("abortPolicy"), r.array("providerSlots").objects("providerSlots").map(::providerSlot),
            )
            ComponentKindModel.BACKEND_PROVIDER -> ProviderComponentModel(
                common, r.array("staticExports").objects("staticExports").map(::staticExport),
                providerBinding(r.obj("providerBinding")), r.array("jobs").objects("jobs").map(::job),
            )
            ComponentKindModel.TOOL -> ToolComponentModel(
                common, r.array("staticExports").objects("staticExports").map(::staticExport),
                r.array("jobs").objects("jobs").map(::job),
            )
            ComponentKindModel.CUSTOM_EXECUTOR -> CustomExecutorComponentModel(
                common, r.array("languages").strings("languages").map(::requireId).notEmpty("languages").unique("language").toSet(),
                r.boolean("webUiSupport"),
            )
        }
    }

    private fun dependency(value: JsonObject): DependencyModel {
        val r = value.reader("dependency", setOf("slotId", "required", "cardinality", "mode", "target", "exportId", "contractId", "contractVersion"))
        r.constant("cardinality", "ONE")
        return DependencyModel(r.slotId("slotId"), r.boolean("required"), r.enum("mode"), selector(r.obj("target")), r.id("exportId"), r.id("contractId"), versionRange(r.obj("contractVersion"), "dependency.contractVersion"))
    }

    private fun selector(value: JsonObject): StableComponentSelectorModel {
        val r = value.reader("componentSelector", setOf("publisherId", "packageId", "componentId"))
        return StableComponentSelectorModel(r.id("publisherId"), r.id("packageId"), r.id("componentId"))
    }

    private fun capabilityExport(value: JsonObject): CapabilityExportModel {
        val r = value.reader("export", setOf("id", "contractId", "contractVersion", "visibility", "entryClass", "operations"))
        return CapabilityExportModel(
            r.id("id"), r.id("contractId"), version(r.obj("contractVersion"), "export.contractVersion"),
            r.array("visibility").strings("visibility").map { enumValue<CapabilityVisibilityModel>(it, "visibility") }.notEmpty("visibility").unique("visibility").toSet(),
            requireClassName(r.string("entryClass")), r.array("operations").objects("operations").map(::operation).notEmpty("operations"),
        )
    }

    private fun operation(value: JsonObject): OperationModel {
        val r = value.reader("operation", setOf("id", "requestSchema", "responseSchema"))
        return OperationModel(r.id("id"), r.id("requestSchema"), r.id("responseSchema"))
    }

    private fun staticExport(value: JsonObject): StaticExportModel {
        val r = value.reader("staticExport", setOf("id", "contractId", "contractVersion", "outputs"))
        return StaticExportModel(r.id("id"), r.id("contractId"), version(r.obj("contractVersion"), "staticExport.contractVersion"), r.array("outputs").objects("staticExport.outputs").map { output ->
            val o = output.reader("staticOutput", setOf("outputId", "resourceId"))
            StaticOutputModel(o.id("outputId"), o.id("resourceId"))
        }.notEmpty("staticExport.outputs"))
    }

    private fun hostOperation(value: JsonObject): HostOperationModel {
        val r = value.reader("hostOperation", setOf("slotId", "contractId", "contractVersion"))
        return HostOperationModel(r.slotId("slotId"), r.id("contractId"), versionRange(r.obj("contractVersion"), "hostOperation.contractVersion"))
    }

    private fun job(value: JsonObject): JobModel {
        val r = value.reader("job", setOf("id", "entryClass", "inputs", "outputs"))
        return JobModel(r.id("id"), requireClassName(r.string("entryClass")), r.array("inputs").objects("job.inputs").map(::ioDeclaration), r.array("outputs").objects("job.outputs").map(::ioDeclaration).notEmpty("job.outputs"))
    }

    private fun ioDeclaration(value: JsonObject): IoDeclarationModel = when (value.requiredString("kind")) {
        "DOCUMENT" -> value.reader("documentIo", setOf("id", "kind", "schemaId", "schemaVersion")).let { r ->
            r.constant("kind", "DOCUMENT"); DocumentIoModel(r.id("id"), r.id("schemaId"), version(r.obj("schemaVersion"), "io.schemaVersion"))
        }
        "ARTIFACT" -> value.reader("artifactIo", setOf("id", "kind", "artifactKind", "target")).let { r ->
            r.constant("kind", "ARTIFACT"); ArtifactIoModel(r.id("id"), r.id("artifactKind"), target(r.obj("target"), "io.target"))
        }
        else -> throw IllegalArgumentException("io.kind is not supported")
    }

    private fun providerSlot(value: JsonObject): ProviderSlotModel {
        val r = value.reader("providerSlot", setOf("id", "required", "contractId", "contractVersion"))
        return ProviderSlotModel(r.slotId("id"), r.boolean("required"), r.id("contractId"), versionRange(r.obj("contractVersion"), "providerSlot.contractVersion"))
    }

    private fun providerBinding(value: JsonObject): ProviderBindingModel {
        val r = value.reader("providerBinding", setOf("parent", "slotId", "contractId", "contractVersion"), setOf("parent", "slotId", "contractId", "contractVersion", "physicalStaticExportId"))
        return ProviderBindingModel(selector(r.obj("parent")), r.slotId("slotId"), r.id("contractId"), version(r.obj("contractVersion"), "providerBinding.contractVersion"), r.optionalString("physicalStaticExportId")?.let(::requireId))
    }
}

private class ObjectReader(
    private val value: JsonObject,
    private val label: String,
    required: Set<String>,
    optional: Set<String>,
) {
    init {
        val allowed = required + optional
        require(value.keys.containsAll(required)) { "$label is missing ${required - value.keys}" }
        require(value.keys.all(allowed::contains)) { "$label has unknown members ${value.keys - allowed}" }
        require(value.values.none { it is JsonNull }) { "$label does not permit null" }
    }

    fun obj(name: String): JsonObject = element(name).asObject("$label.$name")
    fun optionalObj(name: String): JsonObject? = value[name]?.asObject("$label.$name")
    fun array(name: String): JsonArray = element(name) as? JsonArray ?: fail("$label.$name must be an array")
    fun string(name: String, maximumLength: Int = Int.MAX_VALUE): String = element(name).asString("$label.$name").also {
        require(it.codePointCount(0, it.length) <= maximumLength) { "$label.$name exceeds $maximumLength characters" }
    }
    fun optionalString(name: String): String? = value[name]?.asString("$label.$name")
    fun id(name: String): String = requireId(string(name))
    fun slotId(name: String): String = requireSlotId(string(name))
    fun path(name: String): String = requirePath(string(name, 1024))
    fun sha256(name: String): String = string(name).also { require(SHA256.matches(it)) { "$label.$name is not lowercase SHA-256" } }
    fun boolean(name: String): Boolean = (element(name) as? JsonPrimitive)?.booleanOrNull ?: fail("$label.$name must be boolean")
    fun long(name: String, minimum: Long = 0, maximum: Long = SAFE_INTEGER): Long = element(name).asLong("$label.$name").also {
        require(it in minimum..maximum) { "$label.$name is out of range" }
    }
    fun int(name: String, minimum: Int = 0, maximum: Int = Int.MAX_VALUE): Int = long(name, minimum.toLong(), maximum.toLong()).toInt()
    inline fun <reified T : Enum<T>> enum(name: String): T = enumValue(string(name), "$label.$name")
    fun constant(name: String, expected: String) = require(string(name) == expected) { "$label.$name must be $expected" }
    fun constant(name: String, expected: Boolean) = require(boolean(name) == expected) { "$label.$name must be $expected" }

    private fun element(name: String): JsonElement = value[name] ?: fail("$label.$name is required")
}

private fun JsonObject.reader(label: String, required: Set<String>, optional: Set<String> = required): ObjectReader =
    ObjectReader(this, label, required, optional)

private fun JsonObject.requiredString(name: String): String = get(name)?.asString(name) ?: fail("$name is required")
private fun JsonElement.asObject(label: String): JsonObject = this as? JsonObject ?: fail("$label must be an object")
private fun JsonElement.asString(label: String): String = (this as? JsonPrimitive)?.takeIf { it.isString }?.content
    ?: fail("$label must be a string")
private fun JsonElement.asLong(label: String): Long = (this as? JsonPrimitive)?.takeUnless { it.isString }?.longOrNull
    ?: fail("$label must be an integer")
private fun JsonArray.objects(label: String): List<JsonObject> = mapIndexed { index, item -> item.asObject("$label[$index]") }
private fun JsonArray.strings(label: String): List<String> = mapIndexed { index, item -> item.asString("$label[$index]") }
private fun <T> List<T>.notEmpty(label: String): List<T> = also { require(it.isNotEmpty()) { "$label must not be empty" } }
private fun <T> List<T>.unique(label: String): List<T> = also { require(it.size == it.toSet().size) { "duplicate $label" } }
private fun fail(message: String): Nothing = throw IllegalArgumentException(message)

private const val SAFE_INTEGER = 9_007_199_254_740_991L
private val ID = Regex("[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*")
private val SLOT_ID = Regex("[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*")
private val ABI = Regex("[A-Za-z0-9][A-Za-z0-9._+-]{0,63}")
private val SHA256 = Regex("[0-9a-f]{64}")
private val PATH = Regex("[A-Za-z0-9._+@-]+(?:/[A-Za-z0-9._+@-]+)*")
private val CLASS_NAME = Regex("(?:[A-Za-z_$][A-Za-z0-9_$]*\\.)+[A-Za-z_$][A-Za-z0-9_$]*")
private val LOCALE = Regex("[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*")

private fun requireId(value: String): String = value.also { require(it.length <= 128 && ID.matches(it)) { "invalid identifier: $it" } }
private fun requireSlotId(value: String): String = value.also { require(it.length <= 96 && SLOT_ID.matches(it)) { "invalid slot identifier: $it" } }
private fun requirePath(value: String): String = value.also {
    require(it.length <= 1024 && PATH.matches(it)) { "invalid package path: $it" }
    require(it.split('/').none { segment -> segment == "." || segment == ".." }) {
        "package path contains a dot segment: $it"
    }
}
private fun requireClassName(value: String): String = value.also { require(it.length <= 240 && CLASS_NAME.matches(it)) { "invalid Java binary class name: $it" } }
private fun requireLocale(value: String): String = value.also {
    require(it.length <= 63 && LOCALE.matches(it)) { "invalid locale: $it" }
    val canonical = Locale.forLanguageTag(it).toLanguageTag()
    require(canonical != "und" && canonical == it) { "locale must be canonical BCP-47: $it" }
}
private inline fun <reified T : Enum<T>> enumValue(value: String, label: String): T =
    enumValues<T>().firstOrNull { it.name == value } ?: fail("$label has unsupported value $value")

private class V2JsonScanner(private val source: String) {
    private var index = 0

    fun scan() {
        skipWhitespace(); parseValue(); skipWhitespace()
        require(index == source.length) { "trailing JSON content at offset $index" }
    }

    private fun parseValue() {
        require(index < source.length) { "missing JSON value" }
        when (source[index]) {
            '{' -> parseObject()
            '[' -> parseArray()
            '"' -> parseString()
            't' -> literal("true")
            'f' -> literal("false")
            'n' -> literal("null")
            '-', in '0'..'9' -> parseInteger()
            else -> fail("invalid JSON value at offset $index")
        }
    }

    private fun parseObject() {
        index++; skipWhitespace(); if (consume('}')) return
        val names = mutableSetOf<String>()
        while (true) {
            require(index < source.length && source[index] == '"') { "object key must be a string at offset $index" }
            val name = parseString()
            require(names.add(name)) { "duplicate JSON member: $name" }
            skipWhitespace(); require(consume(':')) { "missing ':' at offset $index" }; skipWhitespace(); parseValue(); skipWhitespace()
            if (consume('}')) return
            require(consume(',')) { "missing ',' at offset $index" }; skipWhitespace()
        }
    }

    private fun parseArray() {
        index++; skipWhitespace(); if (consume(']')) return
        while (true) {
            parseValue(); skipWhitespace(); if (consume(']')) return
            require(consume(',')) { "missing ',' at offset $index" }; skipWhitespace()
        }
    }

    private fun parseString(): String {
        require(consume('"'))
        val output = StringBuilder()
        while (index < source.length) {
            when (val c = source[index++]) {
                '"' -> return output.toString().also(::requireNfcAndScalar)
                '\\' -> output.append(parseEscape())
                else -> {
                    require(c.code >= 0x20) { "control character in JSON string" }
                    output.append(c)
                }
            }
        }
        fail("unterminated JSON string")
    }

    private fun parseEscape(): Char = when (val c = source.getOrNull(index++) ?: fail("unterminated JSON escape")) {
        '"', '\\', '/' -> c
        'b' -> '\b'
        'f' -> '\u000c'
        'n' -> '\n'
        'r' -> '\r'
        't' -> '\t'
        'u' -> {
            require(index + 4 <= source.length) { "short unicode escape" }
            source.substring(index, index + 4).toIntOrNull(16)?.toChar().also { index += 4 }
                ?: fail("invalid unicode escape")
        }
        else -> fail("invalid JSON escape: \\$c")
    }

    private fun parseInteger() {
        val start = index
        consume('-')
        require(index < source.length)
        if (consume('0')) {
            require(index == source.length || source[index] !in '0'..'9') { "leading zero in JSON number" }
        } else {
            require(source[index] in '1'..'9') { "invalid integer at offset $index" }
            while (index < source.length && source[index] in '0'..'9') index++
        }
        require(index == source.length || source[index] != '.' && source[index] != 'e' && source[index] != 'E') {
            "manifest numbers must be integers"
        }
        val value = source.substring(start, index).toLongOrNull() ?: fail("integer outside signed 64-bit range")
        require(value in -SAFE_INTEGER..SAFE_INTEGER) { "integer outside I-JSON safe range" }
    }

    private fun literal(value: String) {
        require(source.regionMatches(index, value, 0, value.length)) { "invalid literal at offset $index" }
        index += value.length
    }

    private fun skipWhitespace() { while (index < source.length && source[index] in setOf(' ', '\t', '\n', '\r')) index++ }
    private fun consume(expected: Char): Boolean = index < source.length && source[index] == expected && run { index++; true }
}

private fun requireNfcAndScalar(value: String) {
    require(Normalizer.isNormalized(value, Normalizer.Form.NFC)) { "all JSON strings must already be NFC" }
    var index = 0
    while (index < value.length) {
        val c = value[index]
        when {
            Character.isHighSurrogate(c) -> {
                require(index + 1 < value.length && Character.isLowSurrogate(value[index + 1])) { "unpaired high surrogate" }
                index += 2
            }
            Character.isLowSurrogate(c) -> fail("unpaired low surrogate")
            else -> index++
        }
    }
}

private object JcsCanonicalizer {
    fun canonicalize(value: JsonElement): String = buildString { appendValue(value) }

    private fun StringBuilder.appendValue(value: JsonElement) {
        when (value) {
            JsonNull -> append("null")
            is JsonObject -> {
                append('{')
                value.entries.sortedBy(Map.Entry<String, JsonElement>::key).forEachIndexed { index, (key, child) ->
                    if (index > 0) append(',')
                    appendString(key); append(':'); appendValue(child)
                }
                append('}')
            }
            is JsonArray -> {
                append('['); value.forEachIndexed { index, child -> if (index > 0) append(','); appendValue(child) }; append(']')
            }
            is JsonPrimitive -> when {
                value.isString -> appendString(value.content)
                value.booleanOrNull != null -> append(value.content)
                else -> append(value.longOrNull ?: fail("JCS manifest number must be an integer"))
            }
        }
    }

    private fun StringBuilder.appendString(value: String) {
        append('"')
        value.forEach { c ->
            when (c) {
                '"' -> append("\\\"")
                '\\' -> append("\\\\")
                '\b' -> append("\\b")
                '\t' -> append("\\t")
                '\n' -> append("\\n")
                '\u000c' -> append("\\f")
                '\r' -> append("\\r")
                else -> if (c.code < 0x20) append("\\u%04x".format(c.code)) else append(c)
            }
        }
        append('"')
    }
}

private fun sha256(input: ByteArray): ByteArray = MessageDigest.getInstance("SHA-256").digest(input)
private fun ByteArray.toLowerHex(): String = joinToString("") { "%02x".format(it.toInt() and 0xff) }
