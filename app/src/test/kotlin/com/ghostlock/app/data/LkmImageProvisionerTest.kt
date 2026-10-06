package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Pure label/asset/destination rules of the runtime LKM provisioning. */
class LkmImageProvisionerTest {

    @Test
    fun `the device release resolves to its KMI label`() {
        /* Device anchor: 5.15.189-android13-8-... => android13-5.15 (kmi 5015). */
        assertEquals("android13-5.15", LkmImageProvisioner.labelFor("5.15.189-android13-8-00016-g51bba4309aac")) 
        assertEquals("android15-6.6", LkmImageProvisioner.labelFor("6.6.118-android15-8-g93e223c276e7-abogki500782043-4k"))
        assertEquals("android14-6.1", LkmImageProvisioner.labelFor("6.1.145-android14-11-maybe-dirty"))
        assertEquals("android16-6.12", LkmImageProvisioner.labelFor("6.12.23-android16-5-g16e473de48a3-abogki462654244-4k"))
    }

    @Test
    fun `a release without a KMI yields no label (explicit failure, no fallback)`() {
        assertNull(LkmImageProvisioner.labelFor(""))
        assertNull(LkmImageProvisioner.labelFor("5.15.189"))
        assertNull(LkmImageProvisioner.labelFor("android13-8-00016"))
        assertNull(LkmImageProvisioner.labelFor("not-a-release"))
    }

    @Test
    fun `asset path and destination follow the manifest rule`() {
        assertEquals("lkm/android13-5.15/ghostlock.ko", LkmImageProvisioner.assetPath("android13-5.15"))
        assertEquals("helper.ko", LkmImageProvisioner.targetFile(java.io.File("/data/files")).name)
        assertEquals("/data/files/helper.ko", LkmImageProvisioner.targetFile(java.io.File("/data/files")).path)
    }

    @Test
    fun `a copy happens only when the bytes differ`() {
        assertTrue(LkmImageProvisioner.needsCopy(null, byteArrayOf(1, 2)))
        assertTrue(LkmImageProvisioner.needsCopy(byteArrayOf(1), byteArrayOf(1, 2)))
        assertFalse(LkmImageProvisioner.needsCopy(byteArrayOf(1, 2), byteArrayOf(1, 2)))
    }
}