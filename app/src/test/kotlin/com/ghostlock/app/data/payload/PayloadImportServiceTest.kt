package com.ghostlock.app.data.payload

import java.io.ByteArrayInputStream
import java.io.File
import java.nio.file.Files
import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * payload batch (a): the bounded, atomic, double-hashed copy. The failure paths
 * matter more than the happy one — a rejected import must leave NOTHING behind.
 */
class PayloadImportServiceTest {

    private fun sha256(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes).joinToString("") { "%02x".format(it) }

    private fun newRoot(): File = Files.createTempDirectory("glk-payload").toFile()

    private fun partFiles(root: File): List<File> =
        root.walkTopDown().filter { it.isFile && it.name.endsWith(".part") }.toList()

    @Test
    fun `a picked file is copied under its digest and reported`() {
        val root = newRoot()
        val service = PayloadImportService(root, PayloadKind.Script)
        try {
            val bytes = "echo hi".toByteArray()
            val imported = service.import("setup.sh") { ByteArrayInputStream(bytes) }
            val result = imported as PayloadImportResult.Imported
            assertEquals("setup.sh", result.displayName)
            assertEquals(bytes.size.toLong(), result.size)
            assertEquals(sha256(bytes), result.sha256)
            assertEquals(
                "payload/script/" + result.sha256.take(12) + "-setup.sh",
                result.relativePath,
            )
            val installed = File(root, result.relativePath)
            assertTrue(installed.isFile)
            assertEquals("echo hi", installed.readText())
            assertTrue("no temporary file may survive", partFiles(root).isEmpty())
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `an oversized file is refused while reading and leaves nothing behind`() {
        val root = newRoot()
        val service = PayloadImportService(root, PayloadKind.Script)
        try {
            val huge = ByteArray((PayloadKind.Script.maxBytes + 1024L).toInt())
            val result = service.import("big.sh") { ByteArrayInputStream(huge) }
            assertTrue(result is PayloadImportResult.Rejected)
            assertTrue((result as PayloadImportResult.Rejected).reason.contains("larger"))
            assertTrue(partFiles(root).isEmpty())
            assertTrue(File(root, "payload/script").listFiles().isNullOrEmpty())
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `a file that changes between the two reads is refused`() {
        val root = newRoot()
        val service = PayloadImportService(root, PayloadKind.Ko)
        try {
            var call = 0
            val result = service.import("mod.ko") {
                call++
                ByteArrayInputStream(if (call == 1) "first".toByteArray() else "second".toByteArray())
            }
            assertTrue(result is PayloadImportResult.Rejected)
            assertTrue((result as PayloadImportResult.Rejected).reason.contains("changed"))
            assertTrue(partFiles(root).isEmpty())
            assertTrue(File(root, "payload/ko").listFiles().isNullOrEmpty())
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `a traversal name is sanitized and a nameless file is refused`() {
        val root = newRoot()
        val service = PayloadImportService(root, PayloadKind.Script)
        try {
            val bytes = "x".toByteArray()
            val imported = service.import("../../etc/pa sswd") { ByteArrayInputStream(bytes) }
            val result = imported as PayloadImportResult.Imported
            assertEquals("pa_sswd", result.displayName)
            assertTrue(result.relativePath.startsWith("payload/script/"))
            assertTrue(File(root, result.relativePath).isFile)

            assertTrue(service.import("..") { ByteArrayInputStream(bytes) } is PayloadImportResult.Rejected)
            assertTrue(service.import("") { ByteArrayInputStream(bytes) } is PayloadImportResult.Rejected)
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun `an unreadable source is refused without creating the bucket`() {
        val root = newRoot()
        val service = PayloadImportService(root, PayloadKind.Ko)
        try {
            val result = service.import("mod.ko") { throw IllegalStateException("gone") }
            assertTrue(result is PayloadImportResult.Rejected)
            assertTrue((result as PayloadImportResult.Rejected).reason.contains("gone"))
            assertTrue(!File(root, "payload/ko").exists())
        } finally {
            root.deleteRecursively()
        }
    }
}
