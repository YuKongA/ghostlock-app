# Shizuku resolves the UserService component and generated typed AIDL boundary at runtime.
-keep class com.ghostlock.app.shizuku.GhostlockUserService { *; }
-keep interface com.ghostlock.app.run.IRunHost { *; }
-keep interface com.ghostlock.app.run.IRunHostCallback { *; }
-keep class com.ghostlock.app.run.IRunHost$Stub { *; }
-keep class com.ghostlock.app.run.IRunHost$Stub$Proxy { *; }
-keep class com.ghostlock.app.run.IRunHostCallback$Stub { *; }

# commons-compress references optional Zstd classes; the app does not use zstd.
-dontwarn com.github.luben.zstd.**
