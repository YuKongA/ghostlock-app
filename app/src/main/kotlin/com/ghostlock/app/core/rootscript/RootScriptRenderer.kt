package com.ghostlock.app.core.rootscript

/** Produces the sole normalized Payload-then-Finish root script text. */
class RootScriptRenderer {
    /**
     * Renders deterministic script text for one pre-created result path.
     *
     * @param plan Fully resolved execution plan; no lookup remains below this boundary.
     * @param resultPath Pre-created result path in the RunHost namespace.
     * @return LF-only script ending with a newline.
     */
    fun renderText(plan: RootScriptExecutionPlan, resultPath: String): String {
        plan.validate()
        return buildString {
            appendLine("#!/system/bin/sh")
            appendLine(ROOT_SCRIPT_HANDOFF_MARKER)
            appendLine("set -u")
            appendLine("RESULT=${PayloadScriptRenderer.shellQuote(resultPath)}")
            appendLine("exec 9>\"\$RESULT\"")
            appendLine("write_item() {")
            appendLine("  printf '%s.%s.state=%s\\n' \"\$1\" \"\$2\" \"\$3\" >&9")
            appendLine("  printf '%s.%s.reason=%s\\n' \"\$1\" \"\$2\" \"\$4\" >&9")
            appendLine("  printf '%s.%s.exit_kind=%s\\n' \"\$1\" \"\$2\" \"\$5\" >&9")
            appendLine("  printf '%s.%s.exit_value=%s\\n' \"\$1\" \"\$2\" \"\$6\" >&9")
            appendLine("}")
            appendLine(
                "printf 'GLR1\\nrun=%s\\ngeneration=%s\\n' " +
                    PayloadScriptRenderer.shellQuote(plan.runId) + " " + plan.generation + " >&9",
            )
            appendLine("PAYLOAD_REQUIRED_OK=1")
            appendLine("PAYLOAD_BLOCKED=0")
            plan.payload.forEach { item ->
                appendLine("# Payload ${item.itemId}")
                appendLine("if [ \"\$PAYLOAD_BLOCKED\" -ne 0 ]; then")
                appendLine(
                    "  write_item payload ${PayloadScriptRenderer.shellQuote(item.itemId)} 4 1 0 0",
                )
                appendLine("else")
                appendLine("  ITEM_EXIT=0")
                val action = PayloadScriptRenderer.render(item)
                action.lines.forEach { line -> appendLine("  $line") }
                appendLine("  if [ \"\$ITEM_EXIT\" -ne 0 ]; then")
                appendLine(
                    "    write_item payload ${PayloadScriptRenderer.shellQuote(item.itemId)} 3 5 1 \"\$ITEM_EXIT\"",
                )
                if (item.required) {
                    appendLine("    PAYLOAD_REQUIRED_OK=0")
                    appendLine("    PAYLOAD_BLOCKED=1")
                }
                appendLine("  elif ${action.acceptanceCondition}; then")
                appendLine(
                    "    write_item payload ${PayloadScriptRenderer.shellQuote(item.itemId)} 2 0 1 0",
                )
                appendLine("  else")
                appendLine(
                    "    write_item payload ${PayloadScriptRenderer.shellQuote(item.itemId)} 5 14 0 0",
                )
                if (item.required) {
                    appendLine("    PAYLOAD_REQUIRED_OK=0")
                    appendLine("    PAYLOAD_BLOCKED=1")
                }
                appendLine("  fi")
                appendLine("fi")
            }
            plan.finish.forEach { item ->
                appendLine("# Finish ${item.itemId}")
                appendLine(
                    "if [ \"\$PAYLOAD_REQUIRED_OK\" -ne 1 ] && " +
                        "[ ${if (item.alwaysRun) 1 else 0} -ne 1 ]; then",
                )
                appendLine(
                    "  write_item finish ${PayloadScriptRenderer.shellQuote(item.itemId)} 4 1 0 0",
                )
                appendLine("else")
                when (item.kind) {
                    RootScriptFinishKind.COMMAND -> appendFinishProcess(item.itemId, item.args)
                    RootScriptFinishKind.SCRIPT -> appendFinishProcess(
                        item.itemId,
                        listOf("/system/bin/sh", requireNotNull(item.stagedPath)) + item.args,
                    )
                    RootScriptFinishKind.REBOOT -> {
                        appendLine(
                            "  write_item finish ${PayloadScriptRenderer.shellQuote(item.itemId)} 1 0 0 0",
                        )
                        appendLine("  printf 'handoff=1\\ncomplete=1\\n' >&9")
                        appendLine("  sync -f /proc/self/fd/9 2>/dev/null || sync")
                        appendLine("  /system/bin/setprop ctl.restart zygote")
                        appendLine("  exit 0")
                    }
                }
                appendLine("fi")
            }
            appendLine("printf 'handoff=0\\ncomplete=1\\n' >&9")
            appendLine("sync -f /proc/self/fd/9 2>/dev/null || sync")
            appendLine("exit 0")
        }
    }

    /** Appends one direct process action and its strict exit fact. */
    private fun StringBuilder.appendFinishProcess(itemId: String, argv: List<String>) {
        require(argv.isNotEmpty() && argv.size <= 32) { "invalid Finish argv" }
        appendLine("  ${argv.joinToString(separator = " ") { PayloadScriptRenderer.shellQuote(it) }}")
        appendLine("  ITEM_EXIT=\$?")
        appendLine("  if [ \"\$ITEM_EXIT\" -eq 0 ]; then")
        appendLine("    write_item finish ${PayloadScriptRenderer.shellQuote(itemId)} 2 0 1 0")
        appendLine("  else")
        appendLine(
            "    write_item finish ${PayloadScriptRenderer.shellQuote(itemId)} 3 5 1 \"\$ITEM_EXIT\"",
        )
        appendLine("  fi")
    }

}
