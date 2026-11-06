package com.ghostlock.app.run;

/** One bounded host-stamped log record; terminal authority remains RunHostSnapshot. */
parcelable RunHostLogRecord {
    String runId;
    long generation;
    long sequence;
    long elapsedRealtimeMs;
    int origin;
    int level;
    String message;
}
