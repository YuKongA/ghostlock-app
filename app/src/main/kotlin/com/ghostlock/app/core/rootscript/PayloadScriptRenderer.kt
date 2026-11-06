package com.ghostlock.app.core.rootscript

/** One Payload action plus the independent acceptance condition evaluated after process exit. */
data class PayloadShellAction(
    val lines: List<String>,
    val acceptanceCondition: String,
)

/** Converts one fully normalized execution item into direct-argv shell text. */
object PayloadScriptRenderer {
    /**
     * Renders one operation without package lookup, directory scanning, or environment-derived argv.
     *
     * @param item Fully resolved item from [RootScriptExecutionPlan].
     * @return Lines that assign `ITEM_EXIT` and the declared acceptance condition.
     */
    fun render(item: RootScriptPayloadExecution): PayloadShellAction {
        val lines = when (item.kind) {
            RootScriptPayloadKind.COMMAND -> renderProcess(item.args)
            RootScriptPayloadKind.SCRIPT -> renderProcess(
                listOf("/system/bin/sh", requireNotNull(item.stagedPath)) + item.args,
            )
            RootScriptPayloadKind.ROOT_MANAGER -> renderProcess(
                listOf(requireNotNull(item.stagedPath)) + item.args,
            )
            RootScriptPayloadKind.LKM -> renderProcess(
                listOf("/system/bin/insmod", requireNotNull(item.stagedPath)) + item.args,
            )
            RootScriptPayloadKind.DETACHED_PROCESS -> renderDetached(
                listOf(requireNotNull(item.stagedPath)) + item.args,
            )
        }
        return PayloadShellAction(
            lines = lines,
            acceptanceCondition = PayloadVerifier.condition(item),
        )
    }

    /** Renders one bounded direct process invocation. */
    private fun renderProcess(argv: List<String>): List<String> {
        require(argv.isNotEmpty() && argv.size <= 32) { "invalid Payload argv" }
        return listOf(argv.joinToString(separator = " ", transform = ::shellQuote), "ITEM_EXIT=\$?")
    }

    /** Renders one explicit detached process launch whose dispatch result remains independently verified. */
    private fun renderDetached(argv: List<String>): List<String> {
        require(argv.isNotEmpty() && argv.size <= 32) { "invalid detached Payload argv" }
        return listOf(
            argv.joinToString(separator = " ", transform = ::shellQuote) + " >/dev/null 2>&1 &",
            "ITEM_EXIT=\$?",
        )
    }

    /** @return Whitespace-delimited argv tokens; quotes and shell expansion intentionally have no syntax. */
    internal fun tokenize(value: String): List<String> =
        value.trim().split(Regex("\\s+")).filter { it.isNotEmpty() }.also { require(it.size <= 32) }

    /** @return Single-quoted shell literal with embedded quotes escaped. */
    internal fun shellQuote(value: String): String = "'" + value.replace("'", "'\\''") + "'"
}
