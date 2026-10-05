# 43284 生产路径：免 App（adb-only）试验台 + 发现与修复（2026-10-05）

> 结论先行：**adb-only 试验台打通**（无需 App、无需解锁），并在生产路径上发现 **3 个真实缺陷**，其中 2 个已修复；
> 第 3 个（trigger 未真正拉起 LKM）**仍未解决**，且揭示此前 staged 的 `LkmLoaded` 可能是**陈旧标记误判**。

## 1. 试验台（免 App 驱动 43284 生产路径）

43284 生产入口需要「会话密钥帧」，而 App 用 `IpSecManager` 造 SA。我们用 root 复刻了等价内核对象：

| 组件 | 做法 |
|---|---|
| XFRM SA | `ip xfrm state add src 127.0.0.1 dst 127.0.0.1 proto esp spi 0x4a1f0011 mode transport enc 'cbc(aes)' <32B> auth-trunc 'hmac(sha256)' <32B> 128 encap espinudp 4500 4500 0.0.0.0` |
| UDP 封装 socket | `tools/espencap/`（新建）：`socket+setsockopt(UDP_ENCAP_ESPINUDP)+bind 127.0.0.1:4500`，对应 `IpSecManager.openUdpEncapsulationSocket()` |
| 会话帧 | 手工构造 84B（version/kind/spi/encap_port=4500/sender_port=4501/icv=16/AES/HMAC/trailer），stdin 流 = `[u32 len][GLKv3 doc][u32 len][frame][ACK × N]`（`\x1eGLK_STATUS_ACK\n`，可预填，省去 App 的 ACK 通道） |
| 文档 | App 写出的 43284 `profile.bin`（`backend=cve_2026_43284`、`steps=3`） |

**注意**：XFRM state 与 socket **不跨重启**（重启后必须重建）；`espencap` 需 `setsid ... </dev/null >/dev/null 2>&1 &` 启动，否则 adb shell 会挂住。

## 2. 修复 ①：生产路径硬编码 `HookGuardPolicy::Reject`

- **现象**（生产路径）：链走到 Hook 阶段即 `error=12 HookFailed`，`hook=0`；而 staged 默认 `Skip` 可成功。
- **根因**：`execution_binding.cpp:203` 硬编码 `Reject`；本机 libc++.so sentry 入口带 `bti c` 守卫，上游规则要求**跳过 +4**（B5-9h 门禁即用 `hook_guard=skip`）。
- **修复**：改为 `Skip`（附注释 + `TODO(S4)`：后续按 S4 设计做成 `backend.cve_2026_43284.hook.guard` profile 字段）。

## 3. 修复 ②：终点标记在 shell 域不可探测

- **现象**：`written=769 verified=769 crash=1 hook=1` 全部成功后，`run.wait` **超时**（`error=15 WaitTimeout`，放宽到 30s 仍如此）。
- **根因**：判定用 `probe_exists(..., "/dev/dfm0")`（`stat`）；该节点由 LKM 的 UMH 脚本以 root `touch` 创建，在 **Enforcing** 下对 **shell 域** `getattr` 被拒（实测 `ls -l /dev/dfm0` → Permission denied），于是**永远探测不到**。
- **修复**：新增 shell 域可读的镜像标记 `/data/local/tmp/.ghostlock_lkm_ok` / `.ghostlock_lkm_fail`（`real_ops.hpp` 常量 + `wait` 判定接受任一；`root_cmd.sh` 成功/失败路径各 `touch` 两者）。手动以 root 跑脚本验证标记确实生成。

## 4. ✅ 已解决：trigger 其实拉起了 LKM；失败是试验台脚本丢 `+x`

- **证据**：清理标记后跑生产路径，`/data/local/tmp/.ghostlock_lkm_ok` **未生成**，且 `root_cmd.sh` 的日志 `/data/local/tmp/.ghostlock_lkm.log` **mtime 未变** → **LKM 的 UMH 脚本根本没有运行**，即 trigger → init 命中 libc++ hook → `insmod` 这条链没有发生。
- **推论**：此前 staged 运行报的 `run.wait outcome=LkmLoaded` **很可能是陈旧标记误判**（设备上遗留的 `/dev/dfm0`，恰好在 LKM 把 SELinux 置 permissive 的窗口内被 `stat` 到）。因此**不能**据此认为 43284 终态已真机通过。
- **已解决（2026-10-05）**：dmesg 给出完整链路——`KernelSU: hook_manager: unmark 9552 exec /vendor/bin/insmod`（hook 命中 → shellcode → insmod）、
  `ghostlock: selinux_state set permissive`（LKM 加载）、`umh exec returned 32256`（= 退出码 **126**：脚本不可执行）、`selinux_state restored to enforcing`。
  根因是**试验台 `adb push` 覆盖 `/data/local/tmp/.ghostlock_lkm_cmd.sh` 时丢掉 `+x` 位**；`chmod 755` 后 `umh exec returned 0`，
  生产路径 **EXIT=0**、标记 `.ghostlock_lkm_ok` 正常生成。详见 `device-gates/43284-production-20261005-pass.md`。
  （这也确认了此前 staged 的 `LkmLoaded` 确为陈旧标记误判——修复 ② 后两条路径判定一致。）

## 5. 复现命令

```sh
# 1) 建 SA + encap socket（root）
su -c "ip xfrm state add src 127.0.0.1 dst 127.0.0.1 proto esp spi 0x4a1f0011 mode transport \
  enc 'cbc(aes)' 0x<AES32> auth-trunc 'hmac(sha256)' 0x<HMAC32> 128 encap espinudp 4500 4500 0.0.0.0"
su -c 'setsid /data/local/tmp/espencap 4500 </dev/null >/dev/null 2>&1 &'
# 2) 推 native + helper.ko + 文档/帧
adb push build/native/ghostlock /data/local/tmp/glk
adb push tools/lkm/ghostlock/out/ghostlock-android13-5.15.ko /data/local/tmp/helper.ko
adb push /tmp/glk-43284/appcall.bin /data/local/tmp/appcall43284.bin
# 3) 生产路径
adb shell 'cd /data/local/tmp && GHOSTLOCK_HOME=/data/local/tmp TMPDIR=/data/local/tmp \
  ./glk --ghostlock-app-call --enable-status-record < /data/local/tmp/appcall43284.bin'
# 4) staged（对照，stdin 只用会话帧）
adb shell 'cd /data/local/tmp && GHOSTLOCK_HOME=/data/local/tmp \
  ./glk --run-cve-2026-43284 /data/local/tmp/helper.ko /vendor/lib64/libbinderdebug.so --stage=full < /data/local/tmp/frame.bin'
```

**安全说明**：合成 SA / 手工帧仅用于**开发门禁**，不进入任何 profile 或生产路径；XFRM/socket 重启即清。
