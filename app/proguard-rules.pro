# Shizuku.newProcess is private API invoked reflectively; without this R8
# strips it under minifyEnabled and every shizuku run falls back to direct exec.
-keepclassmembers class rikka.shizuku.Shizuku {
    private static rikka.shizuku.ShizukuRemoteProcess newProcess(java.lang.String[], java.lang.String[], java.lang.String);
}
