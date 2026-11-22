package com.ghostlock.app.run;

import android.os.ParcelFileDescriptor;
import com.ghostlock.app.run.FinishItemParcel;
import com.ghostlock.app.run.PayloadItemParcel;
import com.ghostlock.app.run.RunInputArtifactParcel;

/** Complete bounded typed request; capability is resolved from the authenticated catalog object. */
parcelable RunHostRequest {
    String runId;
    long generation;
    String backendKey;
    ParcelFileDescriptor pluginArchiveFd;
    String pluginContentSha256;
    String profileArtifactId;
    List<PayloadItemParcel> payloadItems;
    List<FinishItemParcel> finishItems;
    List<RunInputArtifactParcel> artifacts;
}
