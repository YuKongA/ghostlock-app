# CPP12h 真机门禁：Multicast（Heap owner：mm sets / leak memfd / SKB buffer）— PASS

- 构建：APK 236；native `328a6415e7f4b1c4926427222b249ee820586ebcf41342dc78adab47189d45f7`
- 入口：Direct（untrusted_app uid=10510）；`enforce=unreadable`（完整 W1→W3）
- 设备：A301SO / 5.15.189-android13-8-00016-g51bba4309aac-ab14546557；CPU main=0 consumer=1
- 日志：`CPP12h-20260917-multicast-pass.native.log`（190 行）
- 结论：**PASS**，与 `CPP12g-multicast-pass`（native `7b60739a…`）行为等价

## 覆盖点

- 6/6 route `status=0 clean=1/1 step=0 errno=0 calls=1 success=1`：W1、W1b scratch repair、W2 cred、W2b prebuilt repair、W3 `TIF_SECCOMP`、W3 seccomp mode。
- 6/6 heap spray `prepare_kernel_page ok attempt=1`（无重试）；`mm spray + kernelsnitch ready` ×6、collision 1.96–1.99s、`threads joined` ×6。
- W1b：`private scratch repaired; releasing quarantine`；W2：`child uid = 0`；W3：`child seccomp filter bypassed (finit_module errno=22)`。
- 交接：`handoff: child=30539 alive=1 sent=1 errno=0`、`enforce=1 (enforcing)`、`KernelSU ready`。
- 无 fallback、无 dirty failure、无 panic；`exploit complete` T+28319ms（CPP12g T+30800ms，差异来自 KernelSnitch 碰撞阶段波动，历史观察项）。

## 本次收编范围

- `mm_ctx` 的 `calloc/free` pid/memfd 数组 → `ghostlock::MmContextSet`（两个 owning `std::vector`；析构只释放内存，close/kill 保持显式）。
- `leak_memfd` → `ghostlock::UniqueFd`；`skb_buffer` → `std::unique_ptr<unsigned char[]>`。
- 提交前反汇编对比：8 个攻击关键函数指令形状全部一致（6 个仅地址注解位移，`multicast_waiter_worker`/`multicast_owner_worker` 严格逐指令一致）；其余差异集中在 `heap_context_init`、`cleanup_page_prepare_state`、`prepare_good_kernel_page` 等 heap 准备路径与 `HeapContext` 布局位移。

## 不可判定项

- 设备温度未记录（日志不可判定）；resident 路线未启用。
