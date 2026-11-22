package com.ghostlock.plugin.api.v1;

/** Resource-free entry factory instantiated by a one-shot RunHost. */
public interface DiagnosticBackendEntryPoint {
    DiagnosticBackendRun createRun() throws PluginException;
}
