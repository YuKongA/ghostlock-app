# Shizuku resolves the UserService component and generated typed AIDL boundary at runtime.
-keep class com.ghostlock.app.shizuku.GhostlockUserService { *; }
-keep interface com.ghostlock.app.run.IRunHost { *; }
-keep interface com.ghostlock.app.run.IRunHostCallback { *; }
-keep class com.ghostlock.app.run.IRunHost$Stub { *; }
-keep class com.ghostlock.app.run.IRunHost$Stub$Proxy { *; }
-keep class com.ghostlock.app.run.IRunHostCallback$Stub { *; }

# JNA resolves the ordinary-C Diagnose symbol from this exact interface method and
# reads the annotated Structure fields by their declared names in minified release.
-keep interface com.ghostlock.app.backend.backends.diagnose.jna.DiagnoseJnaLibrary { *; }
-keep class com.ghostlock.app.backend.backends.diagnose.jna.DiagnoseJnaLibrary$* { *; }

# JNA's upstream Android guidance preserves its reflection/JNI surface and every
# application mapping type derived from that surface in minified builds.
-dontwarn java.awt.*
-keep class com.sun.jna.* { *; }
-keep class * extends com.sun.jna.* { *; }
-keepclassmembers class * extends com.sun.jna.* { public *; }
-dontwarn com.sun.jna.**

# commons-compress references optional Zstd classes; the app does not use zstd.
-dontwarn com.github.luben.zstd.**
