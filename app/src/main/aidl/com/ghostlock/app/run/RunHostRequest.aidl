package com.ghostlock.app.run;

import com.ghostlock.app.run.FinishItemParcel;
import com.ghostlock.app.run.PayloadItemParcel;
import com.ghostlock.app.run.RunInputArtifactParcel;

/** Complete bounded typed request; capability is resolved from the immutable registry. */
parcelable RunHostRequest {
    String runId;
    long generation;
    String backendKey;
    String profileArtifactId;
    List<PayloadItemParcel> payloadItems;
    List<FinishItemParcel> finishItems;
    List<RunInputArtifactParcel> artifacts;
}
