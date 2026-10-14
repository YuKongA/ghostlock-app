package com.ghostlock.app.tempui

import android.app.Activity
import android.os.Bundle
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import com.ghostlock.app.jni.Native

/**
 * 最小临时 UI：只做诊断，不做业务。
 *
 * 用户裁定（2026-10-14）：旧 Kotlin UI / profile 层全面废弃，测试期间只保留一个临时界面。
 * 它的职责只有两件：
 *  1. 报告 JNI 边界库当前是否可加载（[Native.ensureLoaded]）；
 *  2. 报告本进程看到的能力契约面（[com.ghostlock.app.core]），供 J1~J6 的批次在真机上肉眼核对。
 *
 * 它**不**宣称任何攻击能力、不发起任何攻击步骤：那些要等对应批次的 native 入口与 Kotlin
 * 步骤引擎落地（见 docs/analysis/jni-two-backend-plan.md）。
 */
class TempUiActivity : Activity() {
    private val log = StringBuilder()
    private lateinit var logView: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        logView = TextView(this).apply {
            setTextIsSelectable(true)
            textSize = 12f
        }
        val probe = Button(this).apply {
            text = "探测 JNI 边界库"
            setOnClickListener { probeNative() }
        }
        val contracts = Button(this).apply {
            text = "报告能力契约面"
            setOnClickListener { reportContracts() }
        }
        val clear = Button(this).apply {
            text = "清空"
            setOnClickListener {
                log.setLength(0)
                render()
            }
        }
        val buttons = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(probe, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(contracts, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(clear, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        }
        val scroll = ScrollView(this).apply { addView(logView) }
        setContentView(
            LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                addView(buttons)
                addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
            },
        )
        append("tempui: 临时界面已启动（旧 UI 已废弃）")
        probeNative()
    }

    private fun probeNative() {
        val loaded = Native.ensureLoaded()
        append("jni: 库名=" + Native.LIBRARY + " 可加载=" + loaded)
        append("jni: 入口=create/invoke/shutdown/destroy（四入口；J1 的 GLKJ 帧 + J2 的会话状态机已落地）")
    }

    private fun reportContracts() {
        append("core: AccessDomain=" + com.ghostlock.app.core.AccessDomain.entries.joinToString())
        append("core: AccessFailure=" + com.ghostlock.app.core.AccessFailure.entries.joinToString())
        append("core: SessionPhase=" + com.ghostlock.app.core.SessionPhase.entries.joinToString())
        append("core: 能力接口=KernelMemoryAccess / PageCacheAccess（描述符由 native 发布）")
    }

    private fun append(line: String) {
        log.append(line).append('\n')
        render()
    }

    private fun render() {
        logView.text = log.toString()
    }
}
