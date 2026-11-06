package com.ghostlock.app.data.finish

import com.ghostlock.app.domain.finish.FinishDefinition
import com.ghostlock.app.domain.finish.FinishDocumentV3
import com.ghostlock.app.domain.finish.FinishItemDefinition
import com.ghostlock.app.domain.finish.FinishKind
import com.ghostlock.app.domain.finish.FinishSeverity
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/** Version-three Finish canonical round-trip and legacy rejection. */
class FinishDocumentRepositoryTest {
    private val definition = FinishDefinition(
        listOf(
            FinishItemDefinition(
                id = "finish-command",
                kind = FinishKind.Command,
                severity = FinishSeverity.Normal,
                alwaysRun = true,
                args = listOf("/system/bin/id"),
            ),
        ),
    )

    @Test
    fun canonicalRoundTripPreservesTypedDefinition() {
        val bytes = FinishDocumentCodec.encode(FinishDocumentV3(definition = definition))
        assertEquals(definition, FinishDocumentCodec.decode(bytes).definition)
    }

    @Test
    fun rejectsLegacyAndRebootThatIsNotLast() {
        val canonical = FinishDocumentCodec.encode(FinishDocumentV3(definition = definition)).toString(Charsets.UTF_8)
        assertThrows(IllegalArgumentException::class.java) {
            FinishDocumentCodec.decode(canonical.replace("\"version\":3", "\"version\":1").toByteArray())
        }
        val invalid = FinishDefinition(
            listOf(
                FinishItemDefinition(
                    id = "finish-reboot",
                    kind = FinishKind.Reboot,
                    severity = FinishSeverity.SafetyCritical,
                    alwaysRun = true,
                ),
                definition.items.single(),
            ),
        )
        assertThrows(IllegalArgumentException::class.java) {
            FinishDocumentCodec.encode(FinishDocumentV3(definition = invalid))
        }
    }

    @Test
    fun rejectsDuplicateIdentityInvalidCommandAndOversizeArgument() {
        val duplicate = FinishDefinition(listOf(definition.items.single(), definition.items.single()))
        assertThrows(IllegalArgumentException::class.java) {
            FinishDocumentCodec.encode(FinishDocumentV3(definition = duplicate))
        }
        val invalidCommand = FinishDefinition(
            listOf(
                FinishItemDefinition(
                    id = "finish-empty-command",
                    kind = FinishKind.Command,
                    severity = FinishSeverity.Normal,
                    alwaysRun = true,
                ),
            ),
        )
        assertThrows(IllegalArgumentException::class.java) {
            FinishDocumentCodec.encode(FinishDocumentV3(definition = invalidCommand))
        }
        val oversize = FinishDefinition(
            listOf(definition.items.single().copy(args = listOf("a".repeat(4097)))),
        )
        assertThrows(IllegalArgumentException::class.java) {
            FinishDocumentCodec.encode(FinishDocumentV3(definition = oversize))
        }
    }

    @Test
    fun rejectsUnknownFieldAndNonCanonicalWhitespace() {
        val canonical = FinishDocumentCodec.encode(FinishDocumentV3(definition = definition))
            .toString(Charsets.UTF_8)
        assertThrows(IllegalArgumentException::class.java) {
            FinishDocumentCodec.decode(canonical.dropLast(1).plus(",\"unknown\":0}").toByteArray())
        }
        assertThrows(IllegalArgumentException::class.java) {
            FinishDocumentCodec.decode(canonical.replaceFirst("{", "{ ").toByteArray())
        }
    }
}
