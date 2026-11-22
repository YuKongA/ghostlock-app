# Shizuku resolves the shell-UID RunHost component and generated typed AIDL boundary at runtime.
-keep class com.ghostlock.app.runhost.shizukushell.ShizukuShellRunHostService { *; }
-keep interface com.ghostlock.app.run.IRunHost { *; }
-keep interface com.ghostlock.app.run.IRunHostCallback { *; }
-keep class com.ghostlock.app.run.IRunHost$Stub { *; }
-keep class com.ghostlock.app.run.IRunHost$Stub$Proxy { *; }
-keep class com.ghostlock.app.run.IRunHostCallback$Stub { *; }

# Dynamically loaded packages were compiled against these exact shared binary names.
-keep class com.ghostlock.plugin.api.v1.** { *; }

# commons-compress references optional Zstd classes; the app does not use zstd.
-dontwarn com.github.luben.zstd.**
