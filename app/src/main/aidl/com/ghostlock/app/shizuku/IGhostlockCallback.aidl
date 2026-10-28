// GhostLock 公用 Shizuku host 回调（所有 backend 共用；oneway）。
package com.ghostlock.app.shizuku;

oneway interface IGhostlockCallback {
    /** 日志/事实行（终端展示用；不参与控制流）。 */
    void onLog(String line);

    /** backend 状态码（由 host 解释；general 层不定义语义）。 */
    void onStatus(int state);

    /** 日志回调丢弃计数（诊断）。 */
    void onDropped(int count);

    /** host 完成/退出码（诊断；终态权威仍在 app Flow）。 */
    void onComplete(int code);
}
