package com.ghostlock.app.data.finish

import com.ghostlock.app.data.payload.CanonicalJson
import com.ghostlock.app.data.payload.requireArray
import com.ghostlock.app.data.payload.requireBoolean
import com.ghostlock.app.data.payload.requireInt
import com.ghostlock.app.data.payload.requireKeys
import com.ghostlock.app.data.payload.requireNullableString
import com.ghostlock.app.data.payload.requireObject
import com.ghostlock.app.data.payload.requireString
import com.ghostlock.app.domain.finish.FINISH_DOCUMENT_VERSION
import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishDocumentV3
import com.ghostlock.app.domain.finish.FinishItemDefinition
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.finish.FinishSeverity
import java.nio.charset.StandardCharsets
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.contentOrNull

/** Canonical JSON codec for the independent version-three Finish document. */
object FinishDocumentCodec {
    private const val MAX_DOCUMENT_BYTES: Int = 64 * 1024
    private val itemIdPattern = Regex("^[a-z][a-z0-9._-]{0,63}$")

    /** @return Canonical version-three document bytes. */
    fun encode(document: FinishDocumentV3): ByteArray {
        validate(document)
        return CanonicalJson.encode(toJson(document)).toByteArray(StandardCharsets.UTF_8)
    }

    /**
     * Decodes only canonical version-three bytes and rejects legacy or partial documents.
     *
     * @param bytes Canonical persistence bytes.
     * @return Typed Finish document.
     */
    fun decode(bytes: ByteArray): FinishDocumentV3 {
        require(bytes.isNotEmpty() && bytes.size <= MAX_DOCUMENT_BYTES) { "finish document size" }
        val root = CanonicalJson.parseCanonical(bytes).requireObject("finish document")
        root.requireKeys(setOf("definition", "version"), "finish document")
        val version = root.requireInt("version")
        require(version == FINISH_DOCUMENT_VERSION) { "unsupported finish version" }
        val definitionObject = root.requireObject("definition")
        definitionObject.requireKeys(setOf("items"), "finish definition")
        val items = definitionObject.requireArray("items").mapIndexed { index, element ->
            val item = element.requireObject("finish item $index")
            item.requireKeys(
                setOf("alwaysRun", "args", "artifactUri", "id", "kind", "severity"),
                "finish item $index",
            )
            FinishItemDefinition(
                id = item.requireString("id"),
                kind = FinishKind.fromDocumentId(item.requireString("kind"))
                    ?: throw IllegalArgumentException("unknown finish kind"),
                severity = FinishSeverity.fromDocumentId(item.requireString("severity"))
                    ?: throw IllegalArgumentException("unknown finish severity"),
                alwaysRun = item.requireBoolean("alwaysRun"),
                artifactUri = item.requireNullableString("artifactUri"),
                args = item.requireArray("args").map { argument ->
                    (argument as? JsonPrimitive)?.takeIf { it.isString }?.contentOrNull
                        ?: throw IllegalArgumentException("finish arg must be a string")
                },
            )
        }
        val document = FinishDocumentV3(
            version = version,
            definition = FinishDefinition(items),
        )
        validate(document)
        require(encode(document).contentEquals(bytes)) { "finish document is not canonical" }
        return document
    }

    /** @return JSON tree for canonical ordering. */
    private fun toJson(document: FinishDocumentV3): JsonObject = JsonObject(
        mapOf(
            "version" to JsonPrimitive(document.version),
            "definition" to JsonObject(
                mapOf("items" to JsonArray(document.definition.items.map(::itemToJson))),
            ),
        ),
    )

    /** @return JSON tree for one Finish item. */
    private fun itemToJson(item: FinishItemDefinition): JsonObject = JsonObject(
        mapOf(
            "id" to JsonPrimitive(item.id),
            "kind" to JsonPrimitive(item.kind.documentId),
            "severity" to JsonPrimitive(item.severity.documentId),
            "alwaysRun" to JsonPrimitive(item.alwaysRun),
            "artifactUri" to item.artifactUri?.let { JsonPrimitive(it) }.orJsonNull(),
            "args" to JsonArray(item.args.map { JsonPrimitive(it) }),
        ),
    )

    /** Validates typed Finish invariants before bytes can become authority. */
    private fun validate(document: FinishDocumentV3) {
        require(document.version == FINISH_DOCUMENT_VERSION) { "unsupported finish version" }
        require(document.definition.items.size <= 32) { "too many finish items" }
        val ids = HashSet<String>()
        var rebootSeen = false
        document.definition.items.forEachIndexed { index, item ->
            require(itemIdPattern.matches(item.id) && ids.add(item.id)) { "invalid finish id" }
            require(item.args.size <= 32) { "too many finish args" }
            require(item.args.all { it.toByteArray(StandardCharsets.UTF_8).size <= 4096 }) {
                "finish arg too large"
            }
            when (item.kind) {
                FinishKind.Command -> require(item.artifactUri == null && item.args.isNotEmpty()) {
                    "command finish shape"
                }
                FinishKind.Script -> require(!item.artifactUri.isNullOrBlank()) { "script finish shape" }
                FinishKind.Reboot -> {
                    require(!rebootSeen && index == document.definition.items.lastIndex) { "reboot must be unique and last" }
                    require(item.alwaysRun && item.artifactUri == null && item.args.isEmpty()) { "reboot finish shape" }
                    rebootSeen = true
                }
            }
        }
    }
}

/** @return Receiver or an explicit JSON null literal. */
private fun JsonPrimitive?.orJsonNull(): JsonElement = this ?: JsonNull
