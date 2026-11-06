package com.ghostlock.app.data.payload

import com.ghostlock.app.domain.payload.PAYLOAD_DOCUMENT_VERSION
import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadDocumentV3
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadRootManagers
import com.ghostlock.app.domain.payload.PayloadSlot
import com.ghostlock.app.domain.payload.defaultAcceptance
import java.io.File
import java.io.FileOutputStream
import java.nio.charset.StandardCharsets
import java.nio.file.StandardCopyOption
import java.util.UUID
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull

/** Canonical JSON codec for the independent version-three Payload document. */
object PayloadDocumentCodec {
    private const val MAX_DOCUMENT_BYTES: Int = 64 * 1024
    private val itemIdPattern = Regex("^[a-z][a-z0-9._-]{0,63}$")

    /**
     * Encodes a typed document into canonical JSON bytes.
     *
     * @param document Version-three Payload document.
     * @return RFC-8785-compatible canonical bytes for the document's bounded value set.
     */
    fun encode(document: PayloadDocumentV3): ByteArray {
        validate(document)
        return CanonicalJson.encode(toJson(document)).toByteArray(StandardCharsets.UTF_8)
    }

    /**
     * Decodes only canonical version-three bytes; unknown, duplicate, legacy, or reordered input fails closed.
     *
     * @param bytes Persisted canonical document.
     * @return Typed document.
     * @throws IllegalArgumentException When bytes are malformed, non-canonical, or unsupported.
     */
    fun decode(bytes: ByteArray): PayloadDocumentV3 {
        require(bytes.isNotEmpty() && bytes.size <= MAX_DOCUMENT_BYTES) { "payload document size" }
        val root = CanonicalJson.parseCanonical(bytes).requireObject("payload document")
        root.requireKeys(setOf("definition", "version"), "payload document")
        val version = root.requireInt("version")
        require(version == PAYLOAD_DOCUMENT_VERSION) { "unsupported payload version" }
        val definitionObject = root.requireObject("definition")
        definitionObject.requireKeys(setOf("items", "rootManager", "safeMode"), "payload definition")
        val items = definitionObject.requireArray("items").mapIndexed { index, element ->
            val item = element.requireObject("payload item $index")
            item.requireKeys(
                setOf(
                    "acceptance",
                    "command",
                    "enabled",
                    "id",
                    "kind",
                    "lkmParams",
                    "lkmUri",
                    "required",
                    "scriptMode",
                    "scriptUri",
                ),
                "payload item $index",
            )
            val kind = PayloadKind.fromDocumentId(item.requireString("kind"))
                ?: throw IllegalArgumentException("unknown payload kind")
            val acceptance = PayloadAcceptance.fromDocumentId(item.requireString("acceptance"))
                ?: throw IllegalArgumentException("unknown payload acceptance")
            PayloadSlot(
                id = item.requireString("id"),
                kind = kind,
                enabled = item.requireBoolean("enabled"),
                required = item.requireBoolean("required"),
                acceptance = acceptance,
                scriptMode = item.requireInt("scriptMode"),
                command = item.requireString("command"),
                scriptUri = item.requireNullableString("scriptUri"),
                lkmUri = item.requireNullableString("lkmUri"),
                lkmParams = item.requireString("lkmParams"),
            )
        }
        val document = PayloadDocumentV3(
            version = version,
            definition = PayloadDefinition(
                rootManager = definitionObject.requireString("rootManager"),
                safeMode = definitionObject.requireBoolean("safeMode"),
                items = items,
            ),
        )
        validate(document)
        require(encode(document).contentEquals(bytes)) { "payload document is not canonical" }
        return document
    }

    /** @return Canonical JSON tree before key ordering. */
    private fun toJson(document: PayloadDocumentV3): JsonObject = JsonObject(
        mapOf(
            "version" to JsonPrimitive(document.version),
            "definition" to JsonObject(
                mapOf(
                    "rootManager" to JsonPrimitive(document.definition.rootManager),
                    "safeMode" to JsonPrimitive(document.definition.safeMode),
                    "items" to JsonArray(document.definition.items.map(::itemToJson)),
                ),
            ),
        ),
    )

    /** @return Canonical JSON tree for one Payload item. */
    private fun itemToJson(item: PayloadSlot): JsonObject = JsonObject(
        mapOf(
            "id" to JsonPrimitive(item.id),
            "kind" to JsonPrimitive(item.kind.documentId),
            "enabled" to JsonPrimitive(item.enabled),
            "required" to JsonPrimitive(item.required),
            "acceptance" to JsonPrimitive(item.acceptance.documentId),
            "scriptMode" to JsonPrimitive(item.scriptMode),
            "command" to JsonPrimitive(item.command),
            "scriptUri" to item.scriptUri?.let { JsonPrimitive(it) }.orJsonNull(),
            "lkmUri" to item.lkmUri?.let { JsonPrimitive(it) }.orJsonNull(),
            "lkmParams" to JsonPrimitive(item.lkmParams),
        ),
    )

    /** Validates the typed contract independently of the JSON parser. */
    private fun validate(document: PayloadDocumentV3) {
        require(document.version == PAYLOAD_DOCUMENT_VERSION) { "unsupported payload version" }
        require(PayloadRootManagers.find(document.definition.rootManager) != null) { "unknown root manager" }
        require(document.definition.items.size <= 64) { "too many payload items" }
        val ids = HashSet<String>()
        document.definition.items.forEach { item ->
            require(itemIdPattern.matches(item.id) && ids.add(item.id)) { "invalid payload id" }
            require(item.acceptance == item.kind.defaultAcceptance()) { "payload acceptance mismatch" }
            require(item.scriptMode == 0 || item.scriptMode == 1) { "invalid script mode" }
            require(item.command.toByteArray(StandardCharsets.UTF_8).size <= 4096) { "payload command too large" }
            require(item.lkmParams.toByteArray(StandardCharsets.UTF_8).size <= 4096) { "payload args too large" }
            when (item.kind) {
                PayloadKind.RootManager -> require(
                    item.scriptMode == 0 && item.command.isEmpty() && item.scriptUri == null && item.lkmUri == null,
                ) { "root-manager payload shape" }
                PayloadKind.Command -> require(item.lkmUri == null && item.lkmParams.isEmpty()) {
                    "command payload shape"
                }
                PayloadKind.Lkm -> require(
                    item.scriptMode == 0 && item.command.isEmpty() && item.scriptUri == null,
                ) { "lkm payload shape" }
            }
        }
    }
}

/** Shared canonical JSON and atomic-file primitives used by the two independent typed repositories. */
internal object CanonicalJson {
    private val parser = Json {
        allowSpecialFloatingPointValues = false
        isLenient = false
    }

    /** @return Parsed element after UTF-8 and canonical-byte identity checks. */
    fun parseCanonical(bytes: ByteArray): JsonElement {
        val text = bytes.toString(StandardCharsets.UTF_8)
        require(text.toByteArray(StandardCharsets.UTF_8).contentEquals(bytes)) { "invalid UTF-8" }
        val element = runCatching { parser.parseToJsonElement(text) }
            .getOrElse { throw IllegalArgumentException("invalid JSON", it) }
        require(encode(element) == text) { "non-canonical JSON" }
        return element
    }

    /**
     * Canonicalizes the bounded integer/string/boolean/null/array/object subset used by v3 documents.
     *
     * @param element JSON value.
     * @return Deterministic JSON with UTF-16 key ordering and no insignificant whitespace.
     */
    fun encode(element: JsonElement): String = when (element) {
        JsonNull -> "null"
        is JsonArray -> element.joinToString(prefix = "[", postfix = "]", separator = ",") { encode(it) }
        is JsonObject -> element.entries.sortedBy { it.key }.joinToString(
            prefix = "{",
            postfix = "}",
            separator = ",",
        ) { (key, value) -> JsonPrimitive(key).toString() + ":" + encode(value) }
        is JsonPrimitive -> element.toString()
    }
}

/** Atomic file helper that never treats SharedPreferences as a second document authority. */
internal object CanonicalDocumentFile {
    /**
     * Publishes bytes by fsyncing a same-directory temporary file before an atomic replacement.
     *
     * @param target Canonical document path.
     * @param bytes Canonical document bytes.
     */
    fun write(target: File, bytes: ByteArray) {
        val parent = requireNotNull(target.parentFile) { "document parent" }
        check(parent.isDirectory || parent.mkdirs()) { "cannot create document directory" }
        val temporary = File(parent, ".${target.name}.${UUID.randomUUID()}.tmp")
        check(temporary.createNewFile()) { "cannot create document temporary" }
        try {
            FileOutputStream(temporary).use { output ->
                output.write(bytes)
                output.flush()
                output.fd.sync()
            }
            java.nio.file.Files.move(
                temporary.toPath(),
                target.toPath(),
                StandardCopyOption.ATOMIC_MOVE,
                StandardCopyOption.REPLACE_EXISTING,
            )
        } catch (error: Exception) {
            throw IllegalStateException("cannot publish canonical document", error)
        } finally {
            if (temporary.exists()) {
                check(temporary.delete()) { "cannot remove unpublished document temporary" }
            }
        }
    }
}

/** @return The receiver or a JSON null literal. */
private fun JsonPrimitive?.orJsonNull(): JsonElement = this ?: JsonNull

/** @return Object receiver or a contract exception. */
internal fun JsonElement.requireObject(label: String): JsonObject =
    this as? JsonObject ?: throw IllegalArgumentException("$label must be an object")

/** Rejects both missing and unknown fields. */
internal fun JsonObject.requireKeys(expected: Set<String>, label: String) {
    require(keys == expected) { "$label fields" }
}

/** @return Required nested object. */
internal fun JsonObject.requireObject(key: String): JsonObject =
    getValue(key).requireObject(key)

/** @return Required nested array. */
internal fun JsonObject.requireArray(key: String): JsonArray =
    getValue(key) as? JsonArray ?: throw IllegalArgumentException("$key must be an array")

/** @return Required string primitive. */
internal fun JsonObject.requireString(key: String): String =
    (getValue(key) as? JsonPrimitive)?.takeIf { it.isString }?.contentOrNull
        ?: throw IllegalArgumentException("$key must be a string")

/** @return Required nullable string primitive. */
internal fun JsonObject.requireNullableString(key: String): String? {
    val value = getValue(key)
    if (value === JsonNull) return null
    return (value as? JsonPrimitive)?.takeIf { it.isString }?.contentOrNull
        ?: throw IllegalArgumentException("$key must be a nullable string")
}

/** @return Required Boolean primitive. */
internal fun JsonObject.requireBoolean(key: String): Boolean =
    (getValue(key) as? JsonPrimitive)?.booleanOrNull
        ?: throw IllegalArgumentException("$key must be a Boolean")

/** @return Required integral primitive. */
internal fun JsonObject.requireInt(key: String): Int =
    (getValue(key) as? JsonPrimitive)?.intOrNull
        ?: throw IllegalArgumentException("$key must be an integer")
