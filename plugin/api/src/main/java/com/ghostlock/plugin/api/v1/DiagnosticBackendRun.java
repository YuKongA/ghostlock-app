package com.ghostlock.plugin.api.v1;

/** Blocking, one-shot diagnostic execution owned by plugin code. */
public interface DiagnosticBackendRun {
    DiagnosticRunResult execute(DiagnosticRunContext context) throws PluginException;

    void close() throws PluginException;
}
