package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

/** S4 R3 canonical layout + alias normalization unit tests (pure JVM). */
class ProfileLayoutTest {
    private fun parse(text: String): ValueMap =
        requireNotNull(HoconSupport.parseValue(text).asValueMap())


    /** P1: plugin section SHAPE validation only (registry semantics live in App). */
    @Test
    fun pluginSectionShapeIsValidated() {
        val digest = "a".repeat(64)
        fun doc(body: String): ValueMap = requireNotNull(
            HoconSupport.parseValue("ghostlock { plugin { " + body + " } }").asValueMap(),
        )

        /* Positive: the frozen key set, dotted ids quoted, params/extract maps. */
        val accepted = doc(
            "\"demo.plugin\" { enabled = true, stage = \"post_terminal\", " +
                "module_path = \"demo.plugin/1.0/demo.plugin.so\", " +
                "module_hash = \"" + digest + "\", " +
                "params { threshold = 7 }, extract { task_offset = 1 } }",
        )
        ProfileLayout.applyNormalize(accepted)

        fun rejects(body: String): IllegalArgumentException = assertThrows(
            IllegalArgumentException::class.java,
        ) { ProfileLayout.applyNormalize(doc(body)) }

        /* Unknown key inside a plugin section. */
        assertTrue(
            rejects("\"demo.plugin\" { mystery = 1 }").message!!.contains("unknown profile key"),
        )
        /* Blank parameter name. */
        assertTrue(
            rejects("\"demo.plugin\" { params { \"\" = 1 } }").message!!.contains("non-blank"),
        )
        /* Malformed id (upper case is not a valid plugin id). */
        assertTrue(
            rejects("\"Demo.Plugin\" { enabled = true }").message!!.contains("invalid plugin id"),
        )
        /* params must be an object. */
        assertTrue(
            rejects("\"demo.plugin\" { params = 1 }").message!!.contains("not an object"),
        )
    }
    @Test
    fun legacyAliasesMapToTheOwnerQualifiedCanonicalForm() {
        val legacy = parse(
            """
            release = "r"
            schema_version = 3
            kernel_major = 5
            backend {
              steps = "w1_w3"
            }
            kernel_phys_load = 123
            route {
              tcp_zerocopy {
                compact_waiter = true
              }
            }
            fallback {
              to = "none"
            }
            kernelsnitch {
              collisions = 8
              mm_struct_sz = 1024
            }
            task_struct {
              prio = 124
            }
            cred {
              copy_size = 176
              caps_offset = 48
            }
            offset {
              init_task = 1
              slide_boot_id = 2
            }
            execution {
              heap {
                prepare_max_attempts = 4
              }
            }
            """.trimIndent(),
        )
        val flat = ProfileLayout.flatten(ProfileLayout.canonicalize(legacy))
        assertEquals(5L, flat["kernel_major"])
        /* R6a: the legacy fallback.to is recognized and ignored. */
        /* HOCON refactor: the pinned legacy token becomes the DECLARED
         * availability, and vr_guard was deleted from the profile surface. */
        /* The canonical map carries the DECLARED availability (the runtime
         * projection's backend.kind is asserted by the runtime test below). */
        assertEquals("tcp_rootchild", flat["available.cve_2026_43499.0"])
        assertEquals(123L, flat["backend.cve_2026_43499.abi.kernel.kernel_phys_load"])
        assertEquals(124L, flat["backend.cve_2026_43499.abi.task_struct.prio"])
        assertEquals(48L, flat["backend.cve_2026_43499.abi.cred.caps_offset"])
        assertEquals(1L, flat["backend.cve_2026_43499.abi.offset.init_task"])
        assertEquals(176L, flat["backend.cve_2026_43499.cred.copy_size"])
        assertEquals(2L, flat["backend.cve_2026_43499.offset.slide_boot_id"])
        assertEquals(8L, flat["backend.cve_2026_43499.kernel.kernelsnitch_collisions"])
        assertEquals(true, flat["backend.cve_2026_43499.kernel.compact_waiter"])
        assertEquals(4L, flat["backend.cve_2026_43499.execution.heap.prepare_max_attempts"])
        assertEquals("tcp_rootchild", flat["backend.cve_2026_43499.steps"])
    }

    @Test
    fun runtimeProjectionRestoresTheLegacyLogicalModel() {
        val legacy = parse(
            """
            release = "r"
            kernel_major = 6
            backend {
              steps = "w1_w3"
            }
            route {
              multicast_waiter {
                waiter_off = 96
                compact_waiter = true
              }
            }
            fallback {
              to = "none"
            }
            kernelsnitch {
              collisions = 8
              mm_struct_sz = 1024
            }
            cred {
              copy_size = 176
              caps_offset = 48
            }
            offset {
              init_task = 1
              slide_boot_id = 2
            }
            """.trimIndent(),
        )
        val runtime = ProfileLayout.normalize(legacy)
        assertEquals(6L, runtime.getLongAt("kernel_major"))
        assertEquals("mcast_rootchild", runtime["backend"].asValueMap()?.get("steps"))
        assertEquals(96L, runtime.getLongAt("route.multicast_waiter.waiter_off"))
        assertEquals(true, runtime.getValueAt("route.multicast_waiter.compact_waiter"))
        assertEquals(48L, runtime.getLongAt("cred.caps_offset"))
        assertEquals(176L, runtime.getLongAt("cred.copy_size"))
        assertEquals(8L, runtime.getLongAt("kernelsnitch.collisions"))
    }

    @Test
    fun canonicalWrapperIsUnwrappedAndValidated() {
        val canonical = parse(
            """
            ghostlock {
              schema_version = 3
              release = "r"
              kernel_major = 5
              available {
                cve_2026_43499 = [ "mcast_rootchild" ]
              }
              backend {
                cve_2026_43499 {
                  steps = "mcast_rootchild"
                  abi {
                    kernel {
                      kernel_phys_load = 7
                    }
                  }
                }
              }
            }
            """.trimIndent(),
        )
        val runtime = ProfileLayout.normalize(canonical)
        assertEquals(5L, runtime.getLongAt("kernel_major"))
        assertEquals(7L, runtime.getLongAt("kernel_phys_load"))
        assertEquals("mcast_rootchild", runtime["backend"].asValueMap()?.get("steps"))
        /* R6a: route fallback is gone from the wire, so nothing materialises. */
        assertNull(runtime["fallback"])
    }

    @Test
    fun legacyFallbackKeysAreRecognizedAndIgnored() {
        val legacy = parse(
            """
            release = "r"
            kernel_major = 6
            route {
              tcp_zerocopy {
                compact_waiter = true
              }
            }
            fallback {
              to = "select_stack"
              route {
                select_stack {
                  waiter_shift = -2
                }
              }
            }
            """.trimIndent(),
        )
        val flat = ProfileLayout.flatten(ProfileLayout.canonicalize(legacy))
        assertNull(flat["backend.cve_2026_43499.route.select_stack.waiter_shift"])
        assertEquals(true, flat["backend.cve_2026_43499.kernel.compact_waiter"])
        val runtime = ProfileLayout.normalize(legacy)
        assertNull(runtime["fallback"])
        assertNull(runtime.getValueAt("route.select_stack.waiter_shift"))
    }

    /* S4 hotfix: LEGACY input is data written by OLDER revisions and cannot be fixed
     * retroactively, so an unknown key is ignored (with a stderr diagnostic) instead of
     * aborting. A fail-closed require() here turned an app upgrade into a launch crash on
     * a real device: `recommend_shizuku: unknown legacy profile key`. Fail-closed remains
     * for canonical input, which this revision authors (see the test below). */
    @Test
    fun unknownLegacyKeyIsIgnoredNotFatal() {
        val legacy = parse(
            """
            release = "r"
            not_a_profile_key = 1
            recommend_shizuku = true
            """.trimIndent(),
        )
        val canonical = ProfileLayout.canonicalize(legacy)
        assertEquals("r", canonical["release"])
        assertNull(canonical["not_a_profile_key"])
        assertNull(canonical["recommend_shizuku"])
        assertEquals("r", ProfileLayout.normalize(legacy)["release"])
    }

    @Test
    fun unknownNestedLegacyKeyIsDroppedNotFatal() {
        val legacy = parse("release = \"r\"\ntask_struct {\n  prio = 1\n  bogus = 2\n}")
        val flat = ProfileLayout.flatten(ProfileLayout.canonicalize(legacy))
        assertTrue(flat.any { (key, value) -> key.endsWith(".prio") && value == 1L })
        assertTrue(flat.keys.none { it.contains("bogus") })
    }

    @Test
    fun unknownCanonicalKeyFailsClosedWithItsPath() {
        val canonical = parse("ghostlock {\n  schema_version = 3\n  bogus = 1\n}")
        val error = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.canonicalize(canonical)
        }
        assertTrue(error.message!!.contains("bogus"))
    }

    /* User ruling 2026-10-06: the v3 shape was never released, so nothing has to
     * be compatible with it — the pre-refactor `selection{}` block is REJECTED.
     * This case used to assert the selection.steps -> backend.<id>.steps
     * migration; the v1 migration point stays LegacyProfileConverter. */
    @Test
    fun legacySelectionBlockIsRejectedNotMigrated() {
        val canonical = parse(
            """
            ghostlock {
              release = "r"
              selection {
                backend = "cve_2026_43499"
                steps = "w1_w3"
                terminal = "root_child"
              }
              backend {
                cve_2026_43499 {
                  route {
                    multicast_waiter {}
                  }
                }
              }
            }
            """.trimIndent(),
        )
        val error = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.canonicalize(canonical)
        }
        assertTrue(error.message!!.contains("selection"))
    }

    @Test
    fun unknownBackendCombinationTokenFailsClosedWithItsText() {
        val canonical = parse(
            """
            ghostlock {
              release = "r"
              available {
                cve_2026_43499 = [ "mcast_rootchild" ]
              }
              backend {
                cve_2026_43499 {
                  steps = "bogus_token"
                }
              }
            }
            """.trimIndent(),
        )
        val error = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.canonicalize(canonical)
        }
        assertTrue(error.message!!.contains("bogus_token"))
    }

    @Test
    fun plannedTokenParsesButIsUnavailable() {
        val canonical = parse(
            """
            ghostlock {
              release = "r"
              available {
                cve_2026_43284 = [ "rootchild" ]
              }
              backend {
                cve_2026_43284 {
                  steps = "rootchild"
                }
              }
            }
            """.trimIndent(),
        )
        val flat = ProfileLayout.flatten(ProfileLayout.canonicalize(canonical))
        assertEquals("rootchild", flat["backend.cve_2026_43284.steps"])
        assertFalse(CombinationCatalog.resolve(BackendKind.Cve2026_43284, "rootchild")!!.available)
    }
}
