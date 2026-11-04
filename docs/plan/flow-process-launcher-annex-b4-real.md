# Flow 进程启动器计划 · 附录 B4-real：真实漏洞 ELF 冷机门禁设计（待授权）

> 状态：**设计草案（2026-11-02）；未执行；需用户明确授权后才可运行真实漏洞 ELF**。
> 归属：主计划 v5 的 B4-real（B4a/B4b fixture 门禁只证明新进程通道与 entry wiring，不证明攻击可执行）。
> 依据：AGENTS §8（攻击路径真机门禁）、A11（触发后不 kill/timeout payload）、用户历史口径（测试机可 panic；E1 协议：冷机/Enforcing/KSU 未加载/opt-in ack）。

## 1 范围与边界

- 两个独立门禁，不混跑：**G-43499-real**（`lib43499-Glued.so` 真实上游 ELF，W1→W2→W3/root handoff）与 **G-43284-real**（`lib43284Glued.so` + AndroidIpSecRuntime，DFRoot/KO/Finish）。
- 运行通道：当前生产路径 = UI/`BackendRunDispatcher` → `FlowLaunchParent.launchSymbol` → App `:flowhost`（A301SO 无 Shizuku，仅 App flavor）。
- **不在本附录**：Shizuku flavor（B3b 环境）、mondrian 5.10 专用 profile/攻击适配（独立计划）。

## 2 前置条件（每次冷机）

1. 用户**明确授权**本门禁（哪台设备、哪个 backend、单次/次数；记录沿革）。
2. 设备冷启动：`reboot` 后 `boot_completed=1`、uptime ≤1800s、`getenforce=Enforcing`、`ro.boot.bootreason=reboot,shell`；KernelSU/KO 未加载（`/proc/modules`、`/sys/module/kernelsu` 不存在）；记录 boot_id、APK/test APK sha256、profile release。
3. 43499：安装当前 debug APK（真实 ELF，不含 no-op fixture 标记），选择与设备 `uname -r` 精确匹配的内置 profile；readiness=Ready；App executor（未授权 Shizuku 不得旁路）。
4. 43284：同前；确认 IpSec runtime 在 :flowhost 可创建（B4b-b 已证）；确认 DFRoot/KO 前置（若 ELF 需要）与 finish 配置。
5. 一次启动、单 route；若同一 boot 已失败/有残留 `:flowhost`/攻击进程，不再重跑，先重启。

## 3 执行与证据（G-43499-real）

1. 记录启动前快照：`cat /proc/uptime`、`boot_id`、`dmesg | tail`、`ps -A | grep flowhost`、SELinux 状态。
2. 通过 UI 触发一次 43499-Glued run（生产入口）；记录：host pid/run_dir、`run.log`（host 输出）、Flow evidence（`backend/exit/profile/host_run_dir`）、launcher 终态（Finished/HandoffRequested/Refused）。
3. 攻击若触发 W1/W2/W3/handoff：记录设备行为（是否重启/panic/root shell/ksud late-load）；panic/重启属可接受，但必须记录 `boot_id` 变化与恢复状态。
4. 门禁后：重启设备恢复；核对 Enforcing、无残留 flowhost/攻击子进程；报告单列「已验证/未验证」，不得以流程成功替代攻击成功。
5. 复核证据：同构建冷机重复性按 AGENTS（同构建可 PASS/panic/PASS 的已知时序需注明）；失败时按 debug 层隔离，不混跑层。

## 4 执行与证据（G-43284-real）

- 同 §3，但额外记录：IpSec runtime create/close（SPI/encap port）、DFRoot/KO 是否加载、root handoff/Finish 结果、soft reboot（Finish 最后动作）；监听 `:flowhost` keepalive 路径（若超时/取消→`ipsec=runtime-leaked`+host 保活，门禁后需 force-stop/重启回收，记录）。
- 失败/panic 后按 A11 不 kill 载荷；先重启再决定是否重跑。

## 5 门禁判据与归档

- 通过条件（单次）：①新进程通道选中且 host pid≠app pid；②生产 entry 被执行（argv/exit/evidence 与真实 ELF 匹配）；③声明的攻击阶段证据（43499: W1/W2/W3/handoff 实际日志；43284: IpSec/DFRoot/Finish）与设备事实一致；④无伪造成功；⑤报告记录 panic/重启（如有）。
- 完整归档 `docs/analysis/device-gates/<日期>-<backend>-real.md` + 原始 logcat/run.log/dmesg；不设篇幅门，单列「已验证/未验证」。
- 未获授权不得运行；门禁只证明该设备/该构建/该 profile 的单次结果，不构成 supported 结论。

## 6 当前环境与待批

- A301SO（`QV770MFGJ1`）：在线、解锁、Enforcing；有 43499/43284 内置 profile（5.15.189 匹配）与 KernelSU 管理器（KO 未加载）。**可用于 G-43499-real / G-43284-real，但需用户授权**。
- mondrian（`b4541958`）：当前离线；5.10 攻击路径与 profile 适配未完成（独立计划）。
- 授权记录：待用户明确回复“授权在 A301SO 运行哪一 backend、几次、是否允许 panic/重启”。
