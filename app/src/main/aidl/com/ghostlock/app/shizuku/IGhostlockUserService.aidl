package com.ghostlock.app.shizuku;

import com.ghostlock.app.shizuku.IGhostlockCallback;
import com.ghostlock.app.shizuku.IGhostlockStatusCallback;

interface IGhostlockUserService {
    void destroy() = 16777114;
    /* sessionFrame is the channel-B 84-byte IpSec SA frame (already length
     * prefixed), or an empty array for every backend that does not carry one.
     * Secrets stay in this process and are written straight to native stdin. */
    void runExploit(int primaryCpu, int consumerCpu, boolean safeMode, boolean forceAttack, in byte[] profileBlob, in byte[] sessionFrame, @nullable String debugDir, IGhostlockCallback callback, IGhostlockStatusCallback statusCallback) = 2;
    /* Explicit staged cve_2026_43284 runner (dev entry only): the sessionFrame
     * is the only stdin input; the staged native entry does not read GLKv3. */
    void runStaged43284(String modulePath, String targetPath, String stage, in byte[] sessionFrame, boolean allowDevTarget, IGhostlockCallback callback) = 3;
}
