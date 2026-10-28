// GhostLock 公用 Shizuku UserService host（所有 backend 共用启动器）。
// - 一个 UserService 进程（shell 域）按 backendId 分发到注册的 ShizukuBackendHost；
// - app 侧 Flow 仍是唯一 run 权威；host 只回执幂等原语；
// - 无 kill/signal 方法；destroy() 事务码约定 = 16777114。
package com.ghostlock.app.shizuku;

import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import com.ghostlock.app.shizuku.IGhostlockCallback;

interface IGhostlockUserService {
    /** 注册 oneway 回调；重复调用替换旧回调。 */
    void setCallback(IGhostlockCallback cb) = 1;

    /** 清除回调。 */
    void clearCallback() = 2;

    /**
     * 选择 backend host 并准备（host 自解释 code 语义）。
     * @param backendId 稳定 wire id（如 "CVE_2026_98260"）。
     * @param config backend 私有配置（Bundle；键由 backend 定义）。
     * @param payload payload 字节（可由 host 限制）。
     * @return host 稳定码（0=OK）。
     */
    int prepare(String backendId, in Bundle config, in byte[] payload) = 3;

    /** 幂等启动；返回 pid/负码（host 解释）。 */
    int start() = 4;

    /** @return host 状态码。 */
    int getState() = 5;

    /** 读日志增量（不删除/不轮转）。 */
    byte[] readLog(long offset, int maxBytes) = 6;

    /** 独立验证事实（UTF-8 key=value 行）。 */
    byte[] verify() = 7;

    /** @return 当前 boot_id；失败空串。 */
    String bootId() = 8;

    /** @return 1 = service 就绪。 */
    int ping() = 9;

    /** @return host 最近具名 reason（OK 时为空串）。 */
    String lastReason() = 10;

    /** 接上 app 日志 fd（项目 log 管线；host 接管 raw fd）。 */
    void attachLog(in ParcelFileDescriptor logFd) = 11;

    /** Shizuku 保留 destroy 事务；host 清状态后 System.exit（不 kill payload）。 */
    void destroy() = 16777114;
}
