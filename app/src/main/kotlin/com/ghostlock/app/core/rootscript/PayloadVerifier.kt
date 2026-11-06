package com.ghostlock.app.core.rootscript

/** Renders declared Payload acceptance checks; process output never becomes success evidence. */
object PayloadVerifier {
    /**
     * Produces a shell condition for one normalized item.
     *
     * @param item Fully resolved execution item.
     * @return Shell condition whose zero exit means the independent acceptance contract is satisfied.
     */
    fun condition(item: RootScriptPayloadExecution): String = when (item.acceptance) {
        RootScriptPayloadAcceptance.EXIT_ZERO,
        RootScriptPayloadAcceptance.TAKEOVER_ACK,
        -> "[ \"\$ITEM_EXIT\" -eq 0 ]"

        RootScriptPayloadAcceptance.DAEMON_ACK ->
            "command -v su >/dev/null 2>&1 || [ -d /sys/module/kernelsu ]"

        RootScriptPayloadAcceptance.MODULE_PRESENT -> {
            val moduleName = requireNotNull(item.stagedPath)
                .substringAfterLast('/')
                .removeSuffix(".ko")
                .removeSuffix(".bin")
            require(moduleName.matches(Regex("^[A-Za-z0-9_-]+$"))) { "invalid module name" }
            "[ -d /sys/module/$moduleName ] || grep -q '^$moduleName ' /proc/modules 2>/dev/null"
        }
    }
}
