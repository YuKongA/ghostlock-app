# Shizuku binds GhostlockUserService by component name and talks over the
# generated AIDL stubs; R8 must keep both sides in release builds.
-keep class com.ghostlock.app.shizuku.GhostlockUserService { *; }
-keep interface com.ghostlock.app.shizuku.IGhostlockUserService { *; }
-keep interface com.ghostlock.app.shizuku.IGhostlockCallback { *; }
-keep class com.ghostlock.app.shizuku.IGhostlockUserService$Stub { *; }
-keep class com.ghostlock.app.shizuku.IGhostlockCallback$Stub { *; }

# commons-compress references optional Zstd classes; the app does not use zstd.
-dontwarn com.github.luben.zstd.**

# 公用 Shizuku host（所有 backend 共用启动器；2026-10-28 用户裁定）：ComponentName 与 AIDL
# Stub/JNI 方法名在运行时解析，R8 不得重命名/裁剪。
-keep class com.ghostlock.app.shizuku.GhostlockUserService { *; }
-keep interface com.ghostlock.app.shizuku.IGhostlockUserService { *; }
-keep interface com.ghostlock.app.shizuku.IGhostlockCallback { *; }
-keep class com.ghostlock.app.shizuku.IGhostlockUserService$Stub { *; }
-keep class com.ghostlock.app.shizuku.IGhostlockUserService$Stub$Proxy { *; }
-keep class com.ghostlock.app.shizuku.IGhostlockCallback$Stub { *; }
-keep class com.ghostlock.app.backend.cve98260.service.Cve98260ShizukuHost { *; }
-keep class com.ghostlock.app.jni.Native98260Jni { *; }
