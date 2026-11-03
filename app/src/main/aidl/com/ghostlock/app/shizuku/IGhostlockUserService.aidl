// GhostLock 公用 Shizuku UserService host（所有 backend 共用启动器）。
// - 一个 UserService 进程（shell 域）按 backendId 分发到注册的 ShizukuBackendHost；
// - app 侧 Flow 仍是唯一 run 权威；host 只回执幂等原语；
// - 无独立 kill/signal 方法；destroy() 触发 backend-owned cleanup，事务码约定 = 16777114。
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

    // --- B3a：通用 FlowHost 分支（迁移期与旧 backend host 方法并存；B5c 才删旧方法） ---
    // wire 契约：显式 id 12..18 禁止重排/复用；错误码见 FlowHostProtocol。

    /** 准备一次 Flow run（symbol/params/payload 校验；单 run one-shot）。 */
    int prepareFlow(String symbol, in Bundle params, in byte[] payload, in ParcelFileDescriptor logFd) = 12;

    /** 启动已准备的 Flow run（后台线程执行；仅 PREPARED 可启动）。 */
    int startFlow() = 13;

    /** @return FlowHostProtocol 状态（IDLE/PREPARED/RUNNING/TERMINAL）。 */
    int getFlowState() = 14;

    /** @return 当前 UserService（shell）进程 pid。 */
    int getFlowPid() = 15;

    /** @return verify 行协议字节（含 host_uid/host_pid/host_domain 证据）；未终态空数组。 */
    byte[] verifyFlow() = 16;

    /** 请求取消 Flow（幂等；不影响旧 backend host）。 */
    void cancelFlow() = 17;

    /** 读取 Flow 内存日志片段；越界参数返回空数组。 */
    byte[] readFlowLog(long offset, int maxBytes) = 18;

    /** Shizuku 保留 destroy 事务；backend 清理自有资源后 UserService 退出， detached payload 策略由 backend 决定。 */
    void destroy() = 16777114;
}
