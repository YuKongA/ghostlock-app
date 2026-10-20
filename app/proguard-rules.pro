# Shizuku binds GhostlockUserService by component name and talks over the
# generated AIDL stubs; R8 must keep both sides in release builds.
-keep class com.ghostlock.app.shizuku.GhostlockUserService { *; }
-keep interface com.ghostlock.app.shizuku.IGhostlockUserService { *; }
-keep interface com.ghostlock.app.shizuku.IGhostlockCallback { *; }
-keep class com.ghostlock.app.shizuku.IGhostlockUserService$Stub { *; }
-keep class com.ghostlock.app.shizuku.IGhostlockCallback$Stub { *; }

# commons-compress references optional Zstd classes; the app does not use zstd.
-dontwarn com.github.luben.zstd.**
