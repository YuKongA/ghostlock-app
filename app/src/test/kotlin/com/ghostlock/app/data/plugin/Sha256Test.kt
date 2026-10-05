package com.ghostlock.app.data.plugin

import java.io.File
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Test

/** The digest the App pins before the probe runs (standard test vectors). */
class Sha256Test {

    private fun temp(bytes: ByteArray): File {
        val file = Files.createTempFile("glk-sha", ".bin").toFile()
        file.writeBytes(bytes)
        return file
    }

    @Test
    fun `matches the published vectors`() {
        assertEquals(
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            Sha256.bytes(ByteArray(0)),
        )
        assertEquals(
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            Sha256.bytes("abc".toByteArray()),
        )
    }

    @Test
    fun `file hashing matches the in-memory digest`() {
        val payload = ByteArray(200_000) { (it % 251).toByte() }
        assertEquals(Sha256.bytes(payload), Sha256.file(temp(payload)))
    }
}
