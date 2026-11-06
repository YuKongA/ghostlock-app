package com.ghostlock.app.run;

/** Stable stage fact without diagnostic text. */
parcelable StageResultParcel {
    int state;
    int reason;
    int exitKind;
    int exitValue;
}
