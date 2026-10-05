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
            recommend_vr_guard = true
            vr_guard {
              tracepoint_funcs = 64
            }
            """.trimIndent(),
        )
        val flat = ProfileLayout.flatten(ProfileLayout.canonicalize(legacy))
        assertEquals(5L, flat["common.kernel_major"])
        /* R6a: the legacy fallback.to is recognized and ignored. */
        assertNull(flat["common.fallback_route"])
        assertEquals(true, flat["common.vr_guard"])
        assertEquals("cve_2026_43499", flat["selection.backend"])
        /* S4 R6b: selection.steps is cancelled from the canonical shape. */
        assertNull(flat["selection.steps"])
        assertEquals("root_child", flat["selection.terminal"])
        assertEquals(123L, flat["platform.abi.kernel.kernel_phys_load"])
        assertEquals(124L, flat["platform.abi.task_struct.prio"])
        assertEquals(48L, flat["platform.abi.cred.caps_offset"])
        assertEquals(1L, flat["platform.abi.offset.init_task"])
        assertEquals(176L, flat["backend.cve_2026_43499.cred.copy_size"])
        assertEquals(2L, flat["backend.cve_2026_43499.offset.slide_boot_id"])
        assertEquals(8L, flat["backend.cve_2026_43499.kernel.kernelsnitch_collisions"])
        assertEquals(true, flat["backend.cve_2026_43499.kernel.compact_waiter"])
        assertEquals(4L, flat["backend.cve_2026_43499.execution.heap.prepare_max_attempts"])
        assertEquals(64L, flat["countermeasure.vivo_vr_guard.tracepoint_funcs"])
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
              selection {
                backend = "cve_2026_43499"
                terminal = "root_child"
              }
              common {
                kernel_major = 5
                fallback_route = "none"
              }
              platform {
                abi {
                  kernel {
                    kernel_phys_load = 7
                  }
                }
              }
              backend {
                cve_2026_43499 {
                  steps = "mcast_rootchild"
                }
              }
            }
            """.trimIndent(),
        )
        val runtime = ProfileLayout.normalize(canonical)
        assertEquals(5L, runtime.getLongAt("kernel_major"))
        assertEquals(7L, runtime.getLongAt("kernel_phys_load"))
        assertEquals("mcast_rootchild", runtime["backend"].asValueMap()?.get("steps"))
        /* R6a: an old canonical common.fallback_route is ignored, not rejected. */
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
        assertNull(flat["common.fallback_route"])
        assertNull(flat["backend.cve_2026_43499.route.select_stack.waiter_shift"])
        assertEquals(true, flat["backend.cve_2026_43499.kernel.compact_waiter"])
        val runtime = ProfileLayout.normalize(legacy)
        assertNull(runtime["fallback"])
        assertNull(runtime.getValueAt("route.select_stack.waiter_shift"))
    }

    @Test
    fun unknownLegacyKeyFailsClosedWithItsPath() {
        val legacy = parse("release = \"r\"\nnot_a_profile_key = 1")
        val error = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.canonicalize(legacy)
        }
        assertTrue(error.message!!.contains("not_a_profile_key"))
    }

    @Test
    fun unknownNestedLegacyKeyFailsClosedWithItsPath() {
        val legacy = parse("release = \"r\"\ntask_struct {\n  prio = 1\n  bogus = 2\n}")
        val error = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.canonicalize(legacy)
        }
        assertTrue(error.message!!.contains("task_struct.bogus"))
    }

    @Test
    fun unknownCanonicalKeyFailsClosedWithItsPath() {
        val canonical = parse("ghostlock {\n  schema_version = 3\n  bogus = 1\n}")
        val error = assertThrows(IllegalArgumentException::class.java) {
            ProfileLayout.canonicalize(canonical)
        }
        assertTrue(error.message!!.contains("bogus"))
    }

    @Test
    fun legacySelectionStepsMigrateToTheOwnerCombinationToken() {
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
        val flat = ProfileLayout.flatten(ProfileLayout.canonicalize(canonical))
        assertNull(flat["selection.steps"])
        assertEquals("mcast_rootchild", flat["backend.cve_2026_43499.steps"])
    }

    @Test
    fun unknownBackendCombinationTokenFailsClosedWithItsText() {
        val canonical = parse(
            """
            ghostlock {
              release = "r"
              selection {
                backend = "cve_2026_43499"
                terminal = "root_child"
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
              selection {
                backend = "cve_2026_43284"
                terminal = "root_child"
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
