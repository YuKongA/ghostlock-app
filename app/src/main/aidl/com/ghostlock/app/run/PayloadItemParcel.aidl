package com.ghostlock.app.run;

/** One ordered, bounded Payload declaration. */
parcelable PayloadItemParcel {
    String itemId;
    int kind;
    boolean required;
    @nullable String artifactId;
    List<String> args;
    int acceptanceKind;
}
