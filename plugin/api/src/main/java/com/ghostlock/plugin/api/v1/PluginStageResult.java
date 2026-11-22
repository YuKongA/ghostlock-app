package com.ghostlock.plugin.api.v1;

import java.util.Objects;

/** Exhaustive revision-1 stage tuple. */
public final class PluginStageResult {
    private final PluginStageState state;
    private final PluginResultReason reason;
    private final PluginExitKind exitKind;
    private final int exitValue;

    public PluginStageResult(
            PluginStageState state,
            PluginResultReason reason,
            PluginExitKind exitKind,
            int exitValue) {
        this.state = Objects.requireNonNull(state, "state");
        this.reason = Objects.requireNonNull(reason, "reason");
        this.exitKind = Objects.requireNonNull(exitKind, "exitKind");
        if (!isAllowed(state, reason, exitKind, exitValue)) {
            throw new IllegalArgumentException(
                    "unsupported stage tuple: " + state + "/" + reason + "/"
                            + exitKind + "/" + exitValue);
        }
        this.exitValue = exitValue;
    }

    public PluginStageState state() {
        return state;
    }

    public PluginResultReason reason() {
        return reason;
    }

    public PluginExitKind exitKind() {
        return exitKind;
    }

    public int exitValue() {
        return exitValue;
    }

    private static boolean isAllowed(
            PluginStageState state,
            PluginResultReason reason,
            PluginExitKind exitKind,
            int exitValue) {
        return switch (state) {
            case SUCCESS -> reason == PluginResultReason.NONE
                    && ((exitKind == PluginExitKind.NONE && exitValue == 0)
                            || (exitKind == PluginExitKind.PROCESS_EXIT && exitValue == 0));
            case FAILED -> switch (reason) {
                case REJECTED -> (exitKind == PluginExitKind.NONE && exitValue == 0)
                        || (exitKind == PluginExitKind.NATIVE_STATUS && exitValue != 0);
                case NOT_READY -> exitKind == PluginExitKind.NONE && exitValue == 0;
                case IO_FAILURE -> (exitKind == PluginExitKind.NONE && exitValue == 0)
                        || (exitKind == PluginExitKind.NATIVE_STATUS && exitValue != 0);
                case PROCESS_EXIT_NONZERO -> exitKind == PluginExitKind.PROCESS_EXIT
                        && exitValue >= 1 && exitValue <= 255;
                case SIGNALLED, ARTIFACT_IDENTITY_MISMATCH ->
                        exitKind == PluginExitKind.NATIVE_STATUS && exitValue != 0;
                default -> false;
            };
            case CANCELLED -> reason == PluginResultReason.CANCELLED
                    && exitKind == PluginExitKind.NONE && exitValue == 0;
            case UNKNOWN -> (reason == PluginResultReason.MALFORMED_RESULT
                    || reason == PluginResultReason.INTERNAL_CONTRACT_FAILURE)
                    && ((exitKind == PluginExitKind.NONE && exitValue == 0)
                            || (exitKind == PluginExitKind.NATIVE_STATUS && exitValue != 0));
            case SKIPPED -> reason == PluginResultReason.NOT_STARTED
                    && exitKind == PluginExitKind.NONE && exitValue == 0;
        };
    }
}
