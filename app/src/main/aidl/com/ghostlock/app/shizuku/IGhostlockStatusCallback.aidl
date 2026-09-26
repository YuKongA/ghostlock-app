package com.ghostlock.app.shizuku;

/**
 * Synchronous status callback: the UserService calls onStatus (blocking) so the
 * app can persist the step before the UserService ACKs the native process.
 */
interface IGhostlockStatusCallback {
    void onStatus(String step, String status);
}
