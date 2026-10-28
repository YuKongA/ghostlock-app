package com.ghostlock.app.backend.cve43499.victim

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ghostlock.app.jni.Native43499Jni
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith

/**
 * task-111：FUTEX_WAIT_REQUEUE_PI/CMP_REQUEUE_PI 机制自测（debug-only，不写内核、不跑 W1）。
 *
 * 三变体：0=val3=0；1=val3=owner_tid；2=waiter 绝对 CLOCK_REALTIME deadline。
 * 结果输出 logcat TAG `GLK-FUTEX`；供 Lead 决策 actor 修复方向（本测试断言仅保证 JNI 返回）。
 */
@RunWith(AndroidJUnit4::class)
class ActorFutexSelfTest {

    @Test
    fun runFutexMechanismVariants() {
        /* task-111（用户规则）：只复刻 legacy 同形一组（legacy2/core/race.cpp:171-189/380）；
         * 不发明变体。 */
        val output = Native43499Jni.actorFutexSelfTest(0)
        assertNotNull("legacy-form self-test null", output)
        output!!
        Log.i(
            TAG,
            "GLK-FUTEX|legacy-form|owner_ret=${output[1]}|owner_errno=${output[2]}" +
                "|owner_tid=${output[3]}|waiter_ret=${output[4]}" +
                "|waiter_errno=${output[5]}|elapsed_us=${output[6]}" +
                "|requeue_ret=${output[7]}|requeue_errno=${output[8]}" +
                "|woke=${output[9]}",
        )
    }

    private companion object {
        const val TAG = "GLK-FUTEX"
    }
}
