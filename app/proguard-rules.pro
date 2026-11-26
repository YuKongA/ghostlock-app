# Shizuku binds LSPrivilegeUserService by component name and talks over the
# generated AIDL stubs; R8 must keep both sides in release builds.
-keep class org.lsposed.lsprivilege.shizuku.LSPrivilegeUserService { *; }
-keep interface org.lsposed.lsprivilege.shizuku.ILSPrivilegeUserService { *; }
-keep interface org.lsposed.lsprivilege.shizuku.ILSPrivilegeCallback { *; }
-keep class org.lsposed.lsprivilege.shizuku.ILSPrivilegeUserService$Stub { *; }
-keep class org.lsposed.lsprivilege.shizuku.ILSPrivilegeCallback$Stub { *; }
