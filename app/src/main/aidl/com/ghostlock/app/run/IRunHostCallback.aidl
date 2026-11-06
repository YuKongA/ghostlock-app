package com.ghostlock.app.run;

import com.ghostlock.app.run.RunHostLogRecord;
import com.ghostlock.app.run.RunHostSnapshot;

/** Serialized oneway log and terminal callback. */
oneway interface IRunHostCallback {
    void onLog(in RunHostLogRecord record) = 1;
    void onTerminal(in RunHostSnapshot snapshot) = 2;
}
