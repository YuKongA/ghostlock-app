package com.ghostlock.app.core

import com.ghostlock.app.core.generated.AttackContractLayouts
import java.io.File
import java.lang.reflect.Modifier
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Test

/** Verifies that Gradle compiled the Kotlin projection of the native D1 manifest. */
class AttackContractGeneratedTest {
    private data class Row(
        val group: String,
        val ordinal: Int,
        val field: String,
        val kind: String,
        val jniSignature: String,
        val fixedBytes: Int,
        val allocationMaxBytes: Int,
        val reader: String,
    )

    /** Loads the native-generated TSV without carrying a second handwritten field list. */
    private fun rows(): List<Row> {
        val candidates = listOf(
            File("jni-attack-contract-fields.tsv"),
            File("build/jni-attack-contract-fields.tsv"),
            File("../build/app/jni-attack-contract-fields.tsv"),
        )
        val manifest = candidates.firstOrNull(File::isFile)
        assertNotNull("missing D1 manifest; run ./gradlew exportJniAttackContract", manifest)
        return manifest!!.readLines().drop(1).filter(String::isNotBlank).map { line ->
            val parts = line.split('\t')
            assertEquals("manifest column count", 8, parts.size)
            Row(
                parts[0],
                parts[1].toInt(),
                parts[2],
                parts[3],
                parts[4],
                parts[5].toInt(),
                parts[6].toInt(),
                parts[7],
            )
        }
    }

    /** Returns every mismatch between manifest rows and the actual generated JVM fields. */
    private fun agreementProblems(group: String, rows: List<Row>): List<String> {
        val type = Class.forName("com.ghostlock.app.core.generated." + group)
        val fields = type.declaredFields.filterNot { it.isSynthetic || Modifier.isStatic(it.modifiers) }
        val problems = mutableListOf<String>()
        if (rows.map(Row::ordinal) != rows.indices.toList()) {
            problems += "non-contiguous ordinal"
        }
        if (fields.size != rows.size) {
            problems += "field count"
            return problems
        }
        for (ordinal in rows.indices) {
            val row = rows[ordinal]
            val field = fields[ordinal]
            val signature = when (field.type) {
                java.lang.Long.TYPE -> "J"
                java.lang.Integer.TYPE -> "I"
                java.lang.Boolean.TYPE -> "Z"
                ByteArray::class.java -> "[B"
                else -> field.type.name
            }
            if (field.name != row.field) problems += "name[" + ordinal + "]"
            if (signature != row.jniSignature) problems += "signature[" + ordinal + "]"
            if (row.reader != "read" + group) problems += "reader[" + ordinal + "]"
        }
        return problems
    }

    /** Instantiates the real generated DTO and verifies each reflected field receives its sentinel. */
    private fun verifySentinels(group: String, rows: List<Row>) {
        val type = Class.forName("com.ghostlock.app.core.generated." + group)
        val fields = type.declaredFields.filterNot { it.isSynthetic || Modifier.isStatic(it.modifiers) }
        val arguments = fields.indices.map { ordinal ->
            when (rows[ordinal].jniSignature) {
                "J" -> 0x4A4E490000000000L + ordinal + 1L
                "I" -> 0x4A000000 + ordinal + 1
                "Z" -> ordinal % 2 != 0
                "[B" -> ByteArray(
                    rows[ordinal].fixedBytes.takeIf { it != 0 } ?: (ordinal % 7 + 1),
                ) { (ordinal + 1).toByte() }
                else -> error("unknown signature " + rows[ordinal].jniSignature)
            }
        }
        val instance = type.declaredConstructors.single().newInstance(*arguments.toTypedArray())
        for (ordinal in fields.indices) {
            val actual = fields[ordinal].get(instance)
            val expected = arguments[ordinal]
            if (expected is ByteArray) {
                assertEquals(
                    "sentinel " + group + "[" + ordinal + "]",
                    expected.toList(),
                    (actual as ByteArray).toList(),
                )
            } else {
                assertEquals("sentinel " + group + "[" + ordinal + "]", expected, actual)
            }
        }
    }

    @Test
    fun generatedKotlinProjectionMatchesNativeManifest() {
        val exported = rows().groupBy(Row::group)
        assertEquals(exported.keys, AttackContractLayouts.groups.keys)
        for ((group, fields) in AttackContractLayouts.groups) {
            val rows = exported.getValue(group)
            assertEquals("field count for " + group, rows.size, fields.size)
            assertEquals("JVM agreement for " + group, emptyList<String>(), agreementProblems(group, rows))
            verifySentinels(group, rows)
            for (ordinal in fields.indices) {
                assertEquals("field name " + group + "[" + ordinal + "]", rows[ordinal].field, fields[ordinal].name)
                assertEquals("field kind " + group + "[" + ordinal + "]", rows[ordinal].kind, fields[ordinal].kind)
                assertEquals(
                    "fixed bytes " + group + "[" + ordinal + "]",
                    rows[ordinal].fixedBytes,
                    fields[ordinal].fixedBytes,
                )
                assertEquals(
                    "max bytes " + group + "[" + ordinal + "]",
                    rows[ordinal].allocationMaxBytes,
                    fields[ordinal].allocationMaxBytes,
                )
            }
            for (ordinal in 0 until rows.lastIndex) {
                val swapped = rows.toMutableList()
                val temporary = swapped[ordinal]
                swapped[ordinal] = swapped[ordinal + 1]
                swapped[ordinal + 1] = temporary
                assertFalse(
                    "swapping " + group + "[" + ordinal + "] must fail agreement",
                    agreementProblems(group, swapped).isEmpty(),
                )
            }
        }
    }
}
