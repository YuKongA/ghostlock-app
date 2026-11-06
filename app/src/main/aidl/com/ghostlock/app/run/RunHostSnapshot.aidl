package com.ghostlock.app.run;

import com.ghostlock.app.run.ItemResultParcel;
import com.ghostlock.app.run.StageResultParcel;

/** Immutable RunHost lifecycle and six-layer result snapshot. */
parcelable RunHostSnapshot {
    String runId;
    long generation;
    int state;
    int lastCode;
    int runCapability;
    int diagnosticOutcome;
    StageResultParcel backendPrimitive;
    StageResultParcel scriptDispatch;
    List<ItemResultParcel> payloadItems;
    StageResultParcel payloadOverall;
    List<ItemResultParcel> finishItems;
    StageResultParcel finishOverall;
    StageResultParcel backendCleanup;
    int terminal;
    long logSequence;
    long droppedLogs;
}
