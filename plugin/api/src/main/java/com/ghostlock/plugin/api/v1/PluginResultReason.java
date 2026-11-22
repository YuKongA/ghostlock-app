package com.ghostlock.plugin.api.v1;

public enum PluginResultReason {
    NONE,
    NOT_STARTED,
    REJECTED,
    NOT_READY,
    IO_FAILURE,
    PROCESS_EXIT_NONZERO,
    SIGNALLED,
    CANCELLED,
    MALFORMED_RESULT,
    ARTIFACT_IDENTITY_MISMATCH,
    INTERNAL_CONTRACT_FAILURE
}
