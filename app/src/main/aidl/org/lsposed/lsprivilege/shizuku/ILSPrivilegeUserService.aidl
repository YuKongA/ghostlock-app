package org.lsposed.lsprivilege.shizuku;

import org.lsposed.lsprivilege.shizuku.ILSPrivilegeCallback;
import org.lsposed.lsprivilege.shizuku.ILSPrivilegeStatusCallback;

interface ILSPrivilegeUserService {
    void destroy() = 16777114;
    void runExploit(int primaryCpu, int consumerCpu, boolean safeMode, boolean forceAttack, in byte[] profileBlob, @nullable String debugDir, ILSPrivilegeCallback callback, ILSPrivilegeStatusCallback statusCallback) = 2;
    void runExploitAsManager(int primaryCpu, int consumerCpu, boolean safeMode, boolean forceAttack, in byte[] profileBlob, @nullable String debugDir, @nullable String managerPackage, ILSPrivilegeCallback callback, ILSPrivilegeStatusCallback statusCallback) = 3;
}
