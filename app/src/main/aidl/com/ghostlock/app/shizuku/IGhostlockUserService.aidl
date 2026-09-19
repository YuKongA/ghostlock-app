package com.ghostlock.app.shizuku;

import com.ghostlock.app.shizuku.IGhostlockCallback;

interface IGhostlockUserService {
    void destroy() = 16777114;
    void runExploit(int primaryCpu, int consumerCpu, boolean safeMode, in byte[] profileBlob, @nullable String debugDir, IGhostlockCallback callback) = 1;
}
