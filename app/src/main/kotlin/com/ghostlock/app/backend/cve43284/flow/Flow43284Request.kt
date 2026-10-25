package com.ghostlock.app.backend.cve43284.flow

import com.ghostlock.app.core.flow.FlowRequest

/**
 * 43284 的一次 run 请求。
 *
 * 当前 43284 的攻击接线尚未完成，因此还没有参数；后续参数只能加在这里，
 * 不得回退到 core 的通用 request。
 */
class Flow43284Request : FlowRequest
