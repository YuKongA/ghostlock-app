package com.ghostlock.app.shizuku;

import com.ghostlock.app.shizuku.IGhostlockCallback;

interface IGhostlockUserService {
    void destroy() = 16777114;
    void runExploit(int primaryCpu, int consumerCpu, boolean safeMode, String profileJson, IGhostlockCallback callback) = 1;
}
