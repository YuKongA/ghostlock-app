package com.ghostlock.app.data.route

internal data class SelectConfig(
    val waiterShift: Long,
    val enterDelayUs: Long,
    val timeoutUs: Long,
) : RouteConfig {
    override fun entries(): List<Pair<String, Long>> = listOf(
        "pselect_waiter_shift" to waiterShift,
        "select_enter_delay_us" to enterDelayUs,
        "select_timeout_us" to timeoutUs,
    )

    override fun apply(key: String, value: Long): RouteConfig = when (key) {
        "pselect_waiter_shift" -> copy(waiterShift = value)
        "select_enter_delay_us" -> copy(enterDelayUs = value)
        "select_timeout_us" -> copy(timeoutUs = value)
        else -> this
    }

    companion object {
        val EMPTY = SelectConfig(0L, 0L, 0L)

        fun from(value: (String) -> Long): SelectConfig = SelectConfig(
            waiterShift = value("pselect_waiter_shift"),
            enterDelayUs = value("execution.routes.select_stack.enter_delay_us"),
            timeoutUs = value("execution.routes.select_stack.timeout_us"),
        )
    }
}
