package com.ghostlock.app.data.route

internal data class SelectConfig(
    val waiterShift: Long,
    val enterDelayUs: UInt,
    val timeoutUs: UInt,
) : RouteConfig {
    override fun entries(): List<Pair<String, Long>> = listOf(
        "pselect_waiter_shift" to waiterShift,
        "select_enter_delay_us" to enterDelayUs.toLong(),
        "select_timeout_us" to timeoutUs.toLong(),
    )

    override fun apply(key: String, value: Long): RouteConfig = when (key) {
        "pselect_waiter_shift" -> copy(waiterShift = value)
        "select_enter_delay_us" -> copy(enterDelayUs = value.toConfigUInt())
        "select_timeout_us" -> copy(timeoutUs = value.toConfigUInt())
        else -> this
    }

    companion object {
        val EMPTY = SelectConfig(0L, 0u, 0u)

        fun from(value: (String) -> Long): SelectConfig = SelectConfig(
            waiterShift = value("pselect_waiter_shift"),
            enterDelayUs = value("execution.routes.select_stack.enter_delay_us").toConfigUInt(),
            timeoutUs = value("execution.routes.select_stack.timeout_us").toConfigUInt(),
        )
    }
}
