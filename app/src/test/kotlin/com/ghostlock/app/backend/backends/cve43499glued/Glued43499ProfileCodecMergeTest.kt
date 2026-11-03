package com.ghostlock.app.backend.backends.cve43499glued

import com.ghostlock.app.data.HoconSupport
import com.ghostlock.app.data.ValueMap
import com.ghostlock.app.data.asValueMap
import com.ghostlock.app.data.copyValue
import com.ghostlock.app.data.valueMapOf
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** C0（H1）：运行时 43499 profile 编码必须经 ProfileMerger 合并 execution 调优。 */
class Glued43499ProfileCodecMergeTest {
    /** 生产资产 5.15.189（无 execution 块）的固定副本（H1 复现场景）。 */
    private val noExecutionProfile: String = """
# GhostLock kernel profile: 5.15.189-android13-8-00016-g51bba4309aac-ab14546557 (HOCON).
release = "5.15.189-android13-8-00016-g51bba4309aac-ab14546557"
schema_version = 1
kernel_major = 5
recommend_shizuku = 1
kernel_phys_load = null
kernel_phys_offset = null
route {
  multicast_waiter {
    waiter_off = 96
    buffer_size = 264
    task_offset = 48
    lock_offset = 56
    compact_waiter = 1
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
  normal_prio = 132
  sched_task_group = 1024
  pi_lock = 2180
  pi_waiters = 2200
  pi_top_task = 2216
  pi_blocked_on = 2224
  pid = 1496
  tgid = 1500
  atomic_flags = 1432
  real_cred = 1936
  cred = 1944
  comm = 1960
  tasks = 1232
  seccomp = 2144
}
cred {
  caps_offset = 48
  copy_size = 176
  usage_offset = null
  usage_value = 256
  caps_count = 3
  caps_value = 2199023255551
  ref0_offset = 128
  ref1_offset = 136
  ref2_offset = 144
  ref3_offset = 152
  ref_count = 4
  ref0_image = -274698454400
  ref1_image = -274696707824
  ref2_image = -274698453008
  ref3_image = -274698454232
}
offset {
  init_task = 46412800
  init_cred = 46126472
  empty_zero_page = 47529984
  root_task_group = 47549120
  selinux_enforcing = 47885704
  selinux_blob_sizes = 35027656
  security_hook_heads = 35018304
  slide_nfulnl_logger = 45096488
  slide_boot_id = 47999001
  slide_loggers_0_1 = 45096280
}
    """.trimIndent()

    /** 同一 profile + 自带 execution（验证 profile 值优先于 tuning）。 */
    private val withExecutionProfile: String = noExecutionProfile + """

        execution {
          recommended_cpus {
            main = 3
            consumer = 4
          }
          selected_cpus {
            main = 3
            consumer = 4
          }
          handoff {
            pre_dispatch_settle_ms = 2000
          }
          stages {
            w1_attempts = 15
          }
          race {
            route_done_timeout_ms = 300000
          }
        }
    """.trimIndent()

    /** assets `execution-tuning.conf` 的等价 execution 值。 */
    private val tuning: ValueMap = executionOf(
        """
        execution {
          recommended_cpus {
            main = 0
            consumer = 1
          }
          race {
            route_wait_ms = 1000
            route_done_timeout_ms = 300000
            setup_settle_us = 50000
            state_poll_interval_us = 1000
          }
          stages {
            w1_attempts = 15
            w1_settle_us = 100000
            w2_attempts = 15
            w3_attempts = 6
          }
          handoff {
            pre_dispatch_settle_ms = 2000
            module_poll_attempts = 30
            enforce_poll_attempts = 200
          }
        }
        """.trimIndent(),
    )

    /** assets `execution-select-stack.conf` 的 select_stack preset。 */
    private val routePresets: Map<String, ValueMap> = mapOf(
        "select_stack" to executionOf(
            """
            execution {
              routes {
                select_stack {
                  enter_delay_us = 50000
                  timeout_us = 200000
                  consumer_max_calls = 1
                  consumer_burst_calls = 1
                }
              }
            }
            """.trimIndent(),
        ).get("routes").asValueMap()!!.get("select_stack").asValueMap()!!,
    )

    @Test
    fun noExecutionProfileGetsTuningDefaultsAndRoutePresets() {
        val merged = Glued43499ProfileCodec.mergeForTest(
            sourceText = noExecutionProfile,
            tuningExecution = tuning,
            routePresets = routePresets,
        )
        assertEquals(2000, intAt(merged, "execution", "handoff", "pre_dispatch_settle_ms"))
        assertEquals(15, intAt(merged, "execution", "stages", "w1_attempts"))
        assertEquals(300000, intAt(merged, "execution", "race", "route_done_timeout_ms"))
        /* profile 无 execution ⇒ pair 取 tuning.recommended_cpus。 */
        assertEquals(0, intAt(merged, "execution", "selected_cpus", "main"))
        assertEquals(1, intAt(merged, "execution", "selected_cpus", "consumer"))
        /* route preset 只补缺失 route 组。 */
        assertEquals(
            50000,
            intAt(merged, "execution", "routes", "select_stack", "enter_delay_us"),
        )
        assertEquals(
            200000,
            intAt(merged, "execution", "routes", "select_stack", "timeout_us"),
        )
    }

    @Test
    fun profileEmbeddedExecutionWinsOverTuning() {
        val merged = Glued43499ProfileCodec.mergeForTest(
            sourceText = withExecutionProfile,
            tuningExecution = tuningWithOverrides(),
            routePresets = routePresets,
        )
        /* profile 自带值优先于 tuning（tuning 的 9999/99 不得覆盖）。 */
        assertEquals(2000, intAt(merged, "execution", "handoff", "pre_dispatch_settle_ms"))
        assertEquals(15, intAt(merged, "execution", "stages", "w1_attempts"))
        assertEquals(300000, intAt(merged, "execution", "race", "route_done_timeout_ms"))
        /* profile.selected_cpus 优先于 tuning.recommended_cpus。 */
        assertEquals(3, intAt(merged, "execution", "selected_cpus", "main"))
        assertEquals(4, intAt(merged, "execution", "selected_cpus", "consumer"))
    }

    @Test
    fun missingOrEmptyTuningFailsClosed() {
        val missing = runCatching {
            Glued43499ProfileCodec.mergeForTest(
                sourceText = noExecutionProfile,
                tuningExecution = null,
                routePresets = emptyMap(),
            )
        }.exceptionOrNull()
        assertTrue(missing is IllegalArgumentException)
        assertEquals("profile-execution-tuning-missing", missing!!.message)

        val empty = runCatching {
            Glued43499ProfileCodec.mergeForTest(
                sourceText = noExecutionProfile,
                tuningExecution = valueMapOf(),
                routePresets = emptyMap(),
            )
        }.exceptionOrNull()
        assertTrue(empty is IllegalArgumentException)
        assertEquals("profile-execution-tuning-missing", empty!!.message)
    }

    @Test
    fun encodeForTestEmitsBinaryFromMergedProfile() {
        val resolved = Glued43499ProfileCodec.encodeForTest(
            sourceText = noExecutionProfile,
            tuningExecution = tuning,
            routePresets = routePresets,
        )
        assertTrue("binary must not be empty", resolved.binary.isNotEmpty())
        assertEquals(
            "5.15.189-android13-8-00016-g51bba4309aac-ab14546557",
            resolved.release,
        )
        assertEquals(noExecutionProfile, resolved.sourceText)
        /* 二进制必须与“无 merge 的旧路径”不同（旧路径 execution 全 0）。 */
        assertNotEquals(0, resolved.binary.size)
    }

    /** @return 带刻意冲突值的 tuning（验证 profile 优先）。 */
    private fun tuningWithOverrides(): ValueMap = executionOf(
        """
        execution {
          recommended_cpus {
            main = 0
            consumer = 1
          }
          handoff { pre_dispatch_settle_ms = 9999 }
          stages { w1_attempts = 99 }
        }
        """.trimIndent(),
    )

    /** @return HOCON 文本中的 `.execution` ValueMap。 */
    private fun executionOf(hocon: String): ValueMap {
        val root = HoconSupport.parseValue(hocon)?.copyValue().asValueMap()
            ?: error("test hocon parse failed")
        return root.get("execution").asValueMap() ?: error("test hocon missing execution")
    }

    /** @return 深层 int 值（path 为 map 键序列）。 */
    private fun intAt(root: ValueMap, vararg path: String): Int {
        var current: Any? = root
        for (key in path) {
            val map = current.asValueMap() ?: error("missing path segment " + key)
            current = map[key]
        }
        return (current as? Number)?.toInt() ?: error("not a number at " + path.joinToString("."))
    }
}
