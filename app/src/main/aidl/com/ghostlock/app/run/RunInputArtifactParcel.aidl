package com.ghostlock.app.run;

import android.os.ParcelFileDescriptor;

/** Read-only input identity and descriptor owned by RunHost after duplication. */
parcelable RunInputArtifactParcel {
    String logicalId;
    int kind;
    long byteCount;
    String sha256;
    ParcelFileDescriptor fd;
}
