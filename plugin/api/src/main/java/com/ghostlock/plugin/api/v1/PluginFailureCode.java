package com.ghostlock.plugin.api.v1;

/** Closed failures that plugin code may report across the shared ABI. */
public enum PluginFailureCode {
    INVALID_INPUT,
    NOT_READY,
    IO_FAILURE,
    CANCELLED,
    CONTRACT_FAILURE
}
