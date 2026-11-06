package com.ghostlock.app.run;

/** One ordered, bounded Finish declaration. */
parcelable FinishItemParcel {
    String itemId;
    int kind;
    int severity;
    boolean alwaysRun;
    @nullable String artifactId;
    List<String> args;
}
