package com.ghostlock.app.shizuku;

import com.ghostlock.app.shizuku.IGhostlockCallback;
import com.ghostlock.app.shizuku.IGhostlockStatusCallback;

interface IGhostlockUserService {
    /* Called by the Shizuku MANAGER -- a third-party process -- when it stops or
     * unbinds this UserService, NOT by our client code. A repository-wide search
     * finding no caller is therefore expected and correct: a member of a
     * cross-process / third-party contract is not dead code merely because this
     * repository does not call it. The implementation exits the service process
     * when the service is idle. */
    void destroy() = 16777114;
    /* sessionFrame is the channel-B 84-byte IpSec SA frame (already length
     * prefixed), or an empty array for every backend that does not carry one.
     * Secrets stay in this process and are written straight to native stdin. */
    void runExploit(int primaryCpu, int consumerCpu, boolean safeMode, boolean forceAttack, in byte[] profileBlob, in byte[] sessionFrame, @nullable String debugDir, IGhostlockCallback callback, IGhostlockStatusCallback statusCallback) = 2;
}
