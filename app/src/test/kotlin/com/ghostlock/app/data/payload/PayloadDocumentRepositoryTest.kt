package com.ghostlock.app.data.payload

import com.ghostlock.app.domain.payload.PayloadDefinition
import com.ghostlock.app.domain.payload.PayloadAcceptance
import com.ghostlock.app.domain.payload.PayloadDocumentV3
import com.ghostlock.app.domain.payload.PayloadKind
import com.ghostlock.app.domain.payload.PayloadSlot
import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

/** Version-three Payload canonical round-trip and legacy rejection. */
class PayloadDocumentRepositoryTest {
    @Test
    fun canonicalRoundTripPreservesTypedDefinition() {
        val definition = PayloadDefinition(
            rootManager = "KernelSU Next",
            safeMode = true,
            items = listOf(PayloadSlot("payload-command", PayloadKind.Command, command = "id")),
        )
        val bytes = PayloadDocumentCodec.encode(PayloadDocumentV3(definition = definition))
        assertEquals(definition, PayloadDocumentCodec.decode(bytes).definition)
    }

    @Test
    fun rejectsLegacyVersionUnknownFieldAndNonCanonicalOrder() {
        val canonical = PayloadDocumentCodec.encode(PayloadDocumentV3(definition = PayloadDefinition())).toString(Charsets.UTF_8)
        assertThrows(IllegalArgumentException::class.java) {
            PayloadDocumentCodec.decode(canonical.replace("\"version\":3", "\"version\":2").toByteArray())
        }
        assertThrows(IllegalArgumentException::class.java) {
            PayloadDocumentCodec.decode(canonical.dropLast(1).plus(",\"unknown\":0}").toByteArray())
        }
        assertThrows(IllegalArgumentException::class.java) {
            PayloadDocumentCodec.decode(canonical.replaceFirst("{\"definition\":", "{\"version\":3,\"definition\":").toByteArray())
        }
    }

    @Test
    fun rejectsDuplicateIdentityAcceptanceMismatchAndUnknownManager() {
        val duplicate = PayloadDefinition(
            items = listOf(
                PayloadSlot("payload-duplicate", PayloadKind.Command, command = "id"),
                PayloadSlot("payload-duplicate", PayloadKind.Command, command = "id"),
            ),
        )
        assertThrows(IllegalArgumentException::class.java) {
            PayloadDocumentCodec.encode(PayloadDocumentV3(definition = duplicate))
        }
        val mismatch = PayloadDefinition(
            items = listOf(
                PayloadSlot(
                    id = "payload-lkm",
                    kind = PayloadKind.Lkm,
                    acceptance = PayloadAcceptance.ExitZero,
                    lkmUri = "content://payload/module",
                ),
            ),
        )
        assertThrows(IllegalArgumentException::class.java) {
            PayloadDocumentCodec.encode(PayloadDocumentV3(definition = mismatch))
        }
        assertThrows(IllegalArgumentException::class.java) {
            PayloadDocumentCodec.encode(
                PayloadDocumentV3(definition = PayloadDefinition(rootManager = "unknown-manager")),
            )
        }
    }

    @Test
    fun rejectsOversizeFieldAndNonCanonicalWhitespace() {
        val oversize = PayloadDefinition(
            items = listOf(
                PayloadSlot("payload-command", PayloadKind.Command, command = "a".repeat(4097)),
            ),
        )
        assertThrows(IllegalArgumentException::class.java) {
            PayloadDocumentCodec.encode(PayloadDocumentV3(definition = oversize))
        }
        val canonical = PayloadDocumentCodec.encode(PayloadDocumentV3(definition = PayloadDefinition()))
            .toString(Charsets.UTF_8)
        assertThrows(IllegalArgumentException::class.java) {
            PayloadDocumentCodec.decode(canonical.replaceFirst("{", "{ ").toByteArray())
        }
    }

    @Test
    fun atomicPublisherRejectsOrdinaryFileAsParent() {
        val directory = Files.createTempDirectory("payload-document-publish")
        val parent = directory.resolve("ordinary-parent")
        Files.writeString(parent, "occupied")
        assertThrows(IllegalStateException::class.java) {
            CanonicalDocumentFile.write(File(parent.toFile(), "payload-v3.json"), byteArrayOf(1))
        }
        assertEquals("occupied", Files.readString(parent))
    }
}
