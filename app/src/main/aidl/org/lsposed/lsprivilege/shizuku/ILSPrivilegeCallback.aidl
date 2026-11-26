package org.lsposed.lsprivilege.shizuku;

oneway interface ILSPrivilegeCallback {
    void onLog(String line);
    void onComplete(int exitCode);
}
