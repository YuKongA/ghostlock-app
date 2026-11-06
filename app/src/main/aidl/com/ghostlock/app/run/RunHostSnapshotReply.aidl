package com.ghostlock.app.run;

import com.ghostlock.app.run.RunHostSnapshot;

/** Synchronous snapshot response; snapshot exists only when code is OK. */
parcelable RunHostSnapshotReply {
    int code;
    @nullable RunHostSnapshot snapshot;
}
