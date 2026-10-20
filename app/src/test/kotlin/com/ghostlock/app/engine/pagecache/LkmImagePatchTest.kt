package com.ghostlock.app.engine.pagecache

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test

/** `LkmImagePatch` 的边界：唯一 magic、尺寸、路径与四个 knob 字段。 */
class LkmImagePatchTest {

    private fun putU32(bytes: ByteArray, at: Int, value: Int) {
        bytes[at] = (value and 0xFF).toByte()
        bytes[at + 1] = ((value ushr 8) and 0xFF).toByte()
        bytes[at + 2] = ((value ushr 16) and 0xFF).toByte()
        bytes[at + 3] = ((value ushr 24) and 0xFF).toByte()
    }

    private fun getU32(bytes: ByteArray, at: Int): Int =
        (bytes[at].toInt() and 0xFF) or
            ((bytes[at + 1].toInt() and 0xFF) shl 8) or
            ((bytes[at + 2].toInt() and 0xFF) shl 16) or
            ((bytes[at + 3].toInt() and 0xFF) shl 24)

    private fun fixture(prefix: Int = 24): ByteArray {
        val image = ByteArray(prefix + LkmImagePatch.KNOB_SIZE + 16) { 0x5A }
        "GLKKNOB1".toByteArray().copyInto(image, prefix)
        putU32(image, prefix + 8, LkmImagePatch.KNOB_SIZE)
        putU32(image, prefix + 12, 1)
        putU32(image, prefix + 16, 0)
        putU32(image, prefix + 20, 1)
        putU32(image, prefix + 24, 9)
        "/dev/null".toByteArray().copyInto(image, prefix + 28)
        return image
    }

    @Test
    fun patchesPathAndFlags() {
        val prefix = 24
        val image = fixture(prefix)
        val before = image.copyOf()
        val path = "/data/user/0/com.ghostlock.app/files/finish/run.sh"
        val out = LkmImagePatch.apply(image, path)

        assertArrayEquals("不改原数组", before, image)
        assertArrayEquals("magic 原样", "GLKKNOB1".toByteArray(), out.copyOfRange(prefix, prefix + 8))
        assertEquals(LkmImagePatch.KNOB_SIZE, getU32(out, prefix + 8))
        assertEquals(1, getU32(out, prefix + 12))
        assertEquals(0, getU32(out, prefix + 16))
        assertEquals("restore_enforce 保留 KO 默认值", 1, getU32(out, prefix + 20))
        assertEquals(path.toByteArray().size, getU32(out, prefix + 24))
        assertEquals(path, String(out.copyOfRange(prefix + 28, prefix + 28 + path.length)))
        assertEquals("NUL 结尾", 0, out[prefix + 28 + path.length].toInt())
    }

    @Test
    fun rejectsMissingMagic() {
        val image = fixture()
        for (index in 0 until 8) image[24 + index] = 0
        try {
            LkmImagePatch.apply(image, "/data/x")
            fail("expected missing magic")
        } catch (expected: LkmImagePatch.LkmPatchException) {
            assertEquals("knob-magic-missing", expected.reason)
        }
    }

    @Test
    fun rejectsDuplicateMagic() {
        val image = fixture()
        val second = image.size - LkmImagePatch.KNOB_SIZE
        "GLKKNOB1".toByteArray().copyInto(image, second)
        try {
            LkmImagePatch.apply(image, "/data/x")
            fail("expected duplicate magic")
        } catch (expected: LkmImagePatch.LkmPatchException) {
            assertEquals("knob-magic-duplicate", expected.reason)
        }
    }

    @Test
    fun rejectsWrongStructSize() {
        val image = fixture()
        putU32(image, 24 + 8, LkmImagePatch.KNOB_SIZE + 4)
        try {
            LkmImagePatch.apply(image, "/data/x")
            fail("expected size mismatch")
        } catch (expected: LkmImagePatch.LkmPatchException) {
            assertEquals("knob-size-mismatch", expected.reason)
        }
    }

    @Test
    fun rejectsRelativeOrOversizedPath() {
        for (path in listOf("data/x", "", "/" + "a".repeat(255))) {
            try {
                LkmImagePatch.apply(fixture(), path)
                fail("expected invalid path: " + path)
            } catch (expected: LkmImagePatch.LkmPatchException) {
                assertEquals("knob-path-invalid", expected.reason)
            }
        }
    }

    @Test
    fun acceptsShortAbsolutePath() {
        val out = LkmImagePatch.apply(fixture(), "/d/run.sh")
        assertTrue(String(out).contains("/d/run.sh"))
    }
}
