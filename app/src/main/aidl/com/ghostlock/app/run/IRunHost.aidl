package com.ghostlock.app.run;

import com.ghostlock.app.run.IRunHostCallback;
import com.ghostlock.app.run.RunHostRequest;
import com.ghostlock.app.run.RunHostSnapshotReply;

/** One-shot typed RunHost lifecycle contract shared by App and Shizuku transports. */
interface IRunHost {
    int setCallback(String runId, long generation, IRunHostCallback callback) = 1;
    int prepare(in RunHostRequest request) = 2;
    int start(String runId, long generation) = 3;
    RunHostSnapshotReply getSnapshot(String runId, long generation) = 4;
    int cancel(String runId, long generation) = 5;
    int requestClose(String runId, long generation) = 6;
    void destroy() = 16777114;
}
