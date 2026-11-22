package com.ghostlock.plugin.api.v1;

import java.util.Objects;

/** Plugin-owned diagnostic facts; Host derives cleanup and the final outcome. */
public final class DiagnosticRunResult {
    private final PluginStageResult backendPrimitive;
    private final PluginStageResult rootScriptDispatch;

    public DiagnosticRunResult(
            PluginStageResult backendPrimitive,
            PluginStageResult rootScriptDispatch) {
        this.backendPrimitive = Objects.requireNonNull(backendPrimitive, "backendPrimitive");
        this.rootScriptDispatch = Objects.requireNonNull(rootScriptDispatch, "rootScriptDispatch");
    }

    public PluginStageResult backendPrimitive() {
        return backendPrimitive;
    }

    public PluginStageResult rootScriptDispatch() {
        return rootScriptDispatch;
    }
}
