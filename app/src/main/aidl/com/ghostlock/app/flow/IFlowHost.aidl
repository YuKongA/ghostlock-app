package com.ghostlock.app.flow;

import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import com.ghostlock.app.flow.IFlowHostCallback;

/**
 * :flowhost 进程内的通用 Flow 执行通道（B2a）。
 *
 * 一次绑定只允许一个 run（one-shot）；所有错误码见 FlowHostProtocol。
 * @note AIDL 编译器要求显式 id 时必须全量赋值：1..9 为方法序，destroy 用计划指定的
 *   16777114；这些 id 是 wire 契约的一部分，禁止重排/复用。
 */
interface IFlowHost {
    /** 设置过程回调；clear 前保持有效。 */
    void setCallback(IFlowHostCallback cb) = 1;

    /** 清除过程回调（幂等）。 */
    void clearCallback() = 2;

    /**
     * 准备一次 run。
     * @return FlowHostProtocol.OK 或具名错误码；失败原因记录在宿主 log/verify 之前的状态。
     */
    int prepare(String symbol, in @nullable Bundle params, in @nullable byte[] payload,
                in @nullable ParcelFileDescriptor logFd) = 3;

    /** 启动已准备的 run；仅 PREPARED 状态可启动。 */
    int start() = 4;

    /** @return 当前状态（IDLE/PREPARED/RUNNING/TERMINAL）。 */
    int getState() = 5;

    /** @return :flowhost 进程 pid（B2b 用于验证每次 run 新进程）。 */
    int getPid() = 6;

    /** 读取宿主内存日志的片段；越界参数返回空数组。 */
    byte[] readLog(long offset, int maxBytes) = 7;

    /** @return 终态 verify 行协议字节（UTF-8）；未终态返回空数组。 */
    byte[] verify() = 8;

    /** 请求取消；幂等，不 kill 攻击子进程（A11）。 */
    void cancel() = 9;

    /** 清理并退出宿主进程（幂等）。 */
    void destroy() = 16777114;
}
