package com.ghostlock.app.shizuku;

import com.ghostlock.app.shizuku.IGhostlockCallback;
import com.ghostlock.app.shizuku.IGhostlockStatusCallback;

interface IGhostlockUserService {
    void destroy() = 16777114;
    /* sessionFrame is the channel-B 84-byte IpSec SA frame (already length
     * prefixed), or an empty array for every backend that does not carry one.
     * Secrets stay in this process and are written straight to native stdin. */
    void runExploit(int primaryCpu, int consumerCpu, boolean safeMode, boolean forceAttack, in byte[] profileBlob, in byte[] sessionFrame, @nullable String debugDir, IGhostlockCallback callback, IGhostlockStatusCallback statusCallback) = 2;
    /* Dev entry (debug builds only, R2b): the SAME document-driven app-call path
     * as runExploit -- the caller builds the effective GLKv3 document in the app
     * process (selection plus lkm_path/carrier_path overrides) -- with the
     * dev-only carrier opt-in appended to the argv. There is no separate staged
     * runner and no stage vocabulary: dev and production share one document, one
     * pipeline and one stdin protocol (blob, optional session frame, status
     * ACK). allowDevTarget only relaxes the carrier-path prefix check. */
    void runDevExploit(in byte[] profileBlob, in byte[] sessionFrame, boolean allowDevTarget, IGhostlockCallback callback, IGhostlockStatusCallback statusCallback) = 3;
}
