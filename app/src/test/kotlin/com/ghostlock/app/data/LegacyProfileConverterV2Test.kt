package com.ghostlock.app.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import com.ghostlock.app.data.profile.Glkv3Encoder
import com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter
import java.io.ByteArrayOutputStream

/**
 * v2 -> v3 migration: the two-hop mapping, presence, two's complement, and the
 * CONVERTER-level fail-closed guards (the framing guards live in WireV2ReaderTest).
 */
class LegacyProfileConverterV2Test {

    /** Test-only v2 writer: no v2 writer exists in the product any more. */
    private class V2(
        val terminal: Int = 1,
        val backend: Int = 1,
        val middleware: Int = 0x0001,
        val release: String = "5.15.189-android13-8-00016-g51bba4309aac",
        val sections: List<Pair<String, List<Pair<String, ULong>>>> = emptyList(),
    ) {
        fun bytes(): ByteArray {
            val out = ByteArrayOutputStream()
            fun le(value: Long, width: Int) {
                for (index in 0 until width) out.write(((value shr (8 * index)) and 0xFF).toInt())
            }
            le(WireV2Reader.MAGIC.toLong(), 4)
            le(2L, 2)
            le(terminal.toLong(), 2)
            le(backend.toLong(), 2)
            le(middleware.toLong(), 2)
            val releaseBytes = release.toByteArray(Charsets.UTF_8)
            le(releaseBytes.size.toLong(), 2)
            le(0L, 2)
            out.write(releaseBytes)
            le(sections.size.toLong(), 2)
            for ((name, entries) in sections) {
                val nameBytes = name.toByteArray(Charsets.UTF_8)
                out.write(nameBytes.size)
                out.write(nameBytes)
                le(entries.size.toLong(), 4)
                for ((key, raw) in entries) {
                    val keyBytes = key.toByteArray(Charsets.UTF_8)
                    out.write(keyBytes.size)
                    out.write(keyBytes)
                    le(raw.toLong(), 8)
                }
            }
            return out.toByteArray()
        }
    }

    private fun canonical(hocon: String) = ProfileLayout.canonicalize(
        requireNotNull(HoconSupport.parseValue(hocon).asValueMap()),
    )

    private fun flat(document: ValueMap) = ProfileLayout.flatten(document)

    /** The equivalent v3 document for the v2 fixture below, owner-qualified. */
    private val equivalentV3 = """
        ghostlock {
          schema_version = 3
          release = "5.15.189-android13-8-00016-g51bba4309aac"
          kernel_major = 6
          backend {
            cve_2026_43499 {
              steps = "mcast_rootchild"
              abi {
                task_struct { prio = 132 }
                offset { init_task = 34677760 }
                kernel { kernel_phys_load = 123 }
                cred { caps_offset = 48 }
              }
              cred { copy_size = 176 }
              offset { slide_boot_id = 1 }
              kernel { kernelsnitch_collisions = 8 }
              route {
                multicast_waiter { waiter_off = 96 }
              }
            }
          }
        }
    """.trimIndent()

    private fun equivalentV2() = V2(
        /* middleware low byte = route id: 3 = multicast_waiter, matching the body
         * (the strict route check rejects a header/body mismatch — it caught
         * exactly this while the fixture still said tcp_zerocopy). */
        middleware = 0x0003,
        sections = listOf(
            "meta" to listOf("kernel_major" to 6uL),
            "task_struct" to listOf("prio" to 132uL),
            "offset" to listOf("init_task" to 34677760uL, "slide_boot_id" to 1uL),
            "kernel" to listOf("kernel_phys_load" to 123uL, "kernelsnitch_collisions" to 8uL),
            "cred" to listOf("caps_offset" to 48uL, "copy_size" to 176uL),
            "route.multicast_waiter" to listOf("waiter_off" to 96uL),
        ),
    ).bytes()

    private fun hex(bytes: ByteArray): String = bytes.joinToString("") { "%02x".format(it) }

    /** The REAL wire bytes of a canonical map through the production path
     * (runtime projection -> NativeProfileDocument -> adapter -> encoder), so key
     * order, integer widths and Bool-vs-UInt shapes are locked too. */
    private fun wireHex(canonicalMap: ValueMap): String {
        val runtime = ProfileLayout.toRuntime(canonicalMap)
        val flat = ProfileLayout.flatten(runtime)
        val route = canonicalMap["backend"].asValueMap()
            ?.get("cve_2026_43499").asValueMap()
            ?.get("route").asValueMap()
            ?.keys?.firstOrNull()?.toString()
        val document = NativeProfileDocument.from(
            release = (runtime["release"] as? String).orEmpty(),
            route = route,
            value = { path -> (flat[path] as? Number)?.toLong() },
            text = { path -> flat[path] as? String },
            bool = { path -> flat[path] as? Boolean },
        )
        return hex(Glkv3Encoder.encode(NativeProfileGlkv3Adapter.adapt(document)))
    }

    @Test
    fun `the v2 document converts to the equivalent v3 canonical map`() {
        val converted = LegacyProfileConverter.convertV2(equivalentV2())
        assertEquals(flat(canonical(equivalentV3)), flat(converted))
        /* Byte-level lock: the two documents must encode to the SAME wire bytes. */
        assertEquals(wireHex(canonical(equivalentV3)), wireHex(converted))
        /* Idempotent: the converted map is already canonical. */
        assertEquals(flat(converted), flat(ProfileLayout.canonicalize(converted)))
    }

    @Test
    fun `presence is key occurrence and a written zero stays zero`() {
        val converted = LegacyProfileConverter.convertV2(
            V2(
                sections = listOf(
                    "meta" to listOf("kernel_major" to 6uL, "safe_mode" to 0uL),
                    "offset" to listOf("slide_boot_id" to 0uL),
                ),
            ).bytes(),
        )
        val flat = flat(converted)
        /* safe_mode is a bool in the manifest: a written 0 converts to false
         * (presence, not value, is what v2 carries). */
        assertEquals(false, flat["safe_mode"])
        assertEquals(0L, flat["backend.cve_2026_43499.offset.slide_boot_id"])
        assertFalse(flat.containsKey("kernel_minor"))
        assertEquals(6L, flat["kernel_major"])
    }

    @Test
    fun `a negative raw stays negative (two's complement)`() {
        val converted = LegacyProfileConverter.convertV2(
            V2(sections = listOf("offset" to listOf("slide_boot_id" to ULong.MAX_VALUE))).bytes(),
        )
        assertEquals(-1L, flat(converted)["backend.cve_2026_43499.offset.slide_boot_id"])
    }

    @Test
    fun `an int-typed field keeps its sign`() {
        /* waiter_off is declared int in the manifest (route.multicast_waiter), so
         * this exercises the Int branch of the value mapping; the uint case is
         * covered separately. Falsified by masking the sign in that branch. */
        val converted = LegacyProfileConverter.convertV2(
            V2(
                middleware = 0x0003,
                sections = listOf(
                    "route.multicast_waiter" to listOf("waiter_off" to ULong.MAX_VALUE),
                ),
            ).bytes(),
        )
        assertEquals(
            -1L,
            flat(converted)["backend.cve_2026_43499.route.multicast_waiter.waiter_off"],
        )
    }

    @Test
    fun u32_width_is_enforced() {
        /* prio is declared uint with width 4: 2^40 must be rejected, not wrapped. */
        rejects(
            V2(sections = listOf("task_struct" to listOf("prio" to (1uL shl 40)))).bytes(),
            "does not fit uint32",
        )
    }

    @Test
    fun int_width_is_enforced() {
        /* waiter_off is declared int with width 4: 2^40 does not fit. */
        rejects(
            V2(
                middleware = 0x0003,
                sections = listOf("route.multicast_waiter" to listOf("waiter_off" to (1uL shl 40))),
            ).bytes(),
            "does not fit int32",
        )
    }

    @Test
    fun bool_width_is_enforced() {
        rejects(V2(sections = listOf("meta" to listOf("safe_mode" to 2uL))).bytes(), "bool target holds 2")
    }

    @Test
    fun `the deleted vr_guard surface is dropped with a diagnostic, not rejected`() {
        val converted = LegacyProfileConverter.convertV2(
            V2(
                sections = listOf(
                    "vr_guard" to listOf("tracepoint_funcs" to 64uL),
                    "meta" to listOf("kernel_major" to 6uL),
                ),
            ).bytes(),
        )
        val flat = flat(converted)
        assertTrue(flat.keys.none { it.contains("vr_guard") })
        assertEquals(6L, flat["kernel_major"])
    }

    private fun rejects(bytes: ByteArray, fragment: String) {
        val error = assertThrows(IllegalArgumentException::class.java) {
            LegacyProfileConverter.convertV2(bytes)
        }
        assertTrue("error must mention '$fragment': " + error.message, error.message.orEmpty().contains(fragment))
    }

    @Test
    fun `unknown sections and keys are rejected with their dotted path`() {
        rejects(V2(sections = listOf("bogus" to listOf("x" to 1uL))).bytes(), "bogus.x")
        /* Hop 1 renamed meta -> common, so the rejected path is spelled the
         * post-hop-1 way: that is direct evidence the first hop ran. */
        rejects(V2(sections = listOf("meta" to listOf("bogus" to 1uL))).bytes(), "common.bogus")
        /* A deleted common member (R6a fallback_route included). */
        rejects(V2(sections = listOf("meta" to listOf("fallback_route" to 1uL))).bytes(), "fallback_route")
        /* Declared nowhere in the native manifest. */
        rejects(
            V2(sections = listOf("backend.cve_2026_43499" to listOf("mystery" to 1uL))).bytes(),
            "backend.cve_2026_43499.mystery",
        )
    }

    @Test
    fun `the route id must match the single route branch in the body`() {
        /* Header says multicast (3) but the body declares tcp_zerocopy. */
        rejects(
            V2(
                middleware = 0x0003,
                sections = listOf("route.tcp_zerocopy" to listOf("attempts" to 3uL)),
            ).bytes(),
            "does not match",
        )
        /* Auto (43284) may not carry a route section at all. */
        rejects(
            V2(
                backend = 6,
                middleware = 0x0000,
                sections = listOf("route.multicast_waiter" to listOf("waiter_off" to 96uL)),
            ).bytes(),
            "Auto route",
        )
    }
}