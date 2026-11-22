package com.ghostlock.plugin.api.v1;

/** Narrow Host services available to a diagnostic run. */
public interface DiagnosticRunContext {
    String runId();

    long generation();

    int processUid();

    int processPid();

    boolean cancellationRequested();

    void emit(PluginLogOrigin origin, PluginLogLevel level, String message);
}
