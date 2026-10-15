# 真机门禁：43284 五步走到目标站点校验（2026-10-11 夜）

> 设备 `QV770MFGJ1`（A301SO · Android 15 · `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）

## 1 方法：帧与 SA 都可以自造

帧只是对内核里那条 SA 的描述（`session_frame.cpp`：84 字节负载 = version/kind/spi/encap_port/sender_port/icv_len/AES/HMAC/trailer `0x34383238`）。
以 root 读活 SA 或自建 SA（`ip xfrm state add … encap espinudp <sport> <dport> 0.0.0.0`）都能得到真帧 ⇒ 不必依赖 App。

踩坑三条：`encap` 端口在输入侧是位置参数（`sport 0x…` 会被拒）；算法名括号在设备 `sh` 里要加引号；`auth-trunc hmac(sha256) … 128` 才与 `icv=16` 一致。

## 2 逐级结果（时间线，每级都是具名拒绝）

- `session-frame-missing` ← fixture 缺 `required_param` + 路径未 NUL 结尾（致命，整条链在门前就断）
- `pagecache-door-unopenable:0→:2` ← 修 NUL 副本后，主机侧的 ENOENT 也说实话
- `step-initialize-landing-failed` ← 活 SA 的 sport 被系统会话占着、bind 失败 ⇒ 改自建 SA
- `step-image-empty` ← 补镜像（patch#1 = 内嵌 helper；carrier = 按名读 `.ko`）
- `step-write-refused:call-failed` ← 带上访问层状态名
- `step-write-refused:…/splice=-111` ← 给 `SpliceOps` 加带默认实现的 `last_error()`，读出 ECONNREFUSED
- `step-displaced-mismatch` ← `dport` 指向已绑定端口后：内核真的按 ESP 处理（`/proc/net/xfrm_stat` 1009→1165）

## 3 还差的一件

生产环境里 ESP-in-UDP 的处理挂在 Android IpSec 服务持有的 `UDP_ENCAP_ESPINUDP` 套接字上；本树 grep `UDP_ENCAP` 零命中 ⇒ 从不创建它。
两条出口：① 恢复 App 侧（帧与套接字一起有）；② 在树里新写该对端（新机制，需点头）。

## 4 副作用（务必知晓）

为排除陈旧页缓存执行了 `adb reboot`。本机 root 来自上一轮用本漏洞加载的 LKM，重启即卸载 ⇒ 现在无 root（`su` 不存在；KernelSU 管理器与应用数据仍在，shell 无权读）。恢复路径就是本项目链条本身，或用户自己的方法。
无 root 下链条仍能走到同一处（`crash_dump64` 是 755、组 shell ⇒ 门可开）。

## 5 复现

```sh
make -C core -B android NDK_TOOLCHAIN=$HOME/Library/Android/sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin
adb push build/native/ghostlock /data/local/tmp/ && adb push /tmp/ghostlock-sa.frame /data/local/tmp/
adb shell "sh /data/local/tmp/mk_sa.sh"   # 建 SA（需 root；含 auth-trunc 128）
adb shell "cd /data/local/tmp && ./ghostlock < wire-43284-steps.framed"
```

## 6 第七段：两支纯 Lua 低层路径的设备结论（2026-10-11 夜）

```
43499 纯 Lua 低层 ：calls=2 handoffs=0 steps=0  refused (write-path-unbuilt)
43284 纯 Lua 低层 ：calls=6 handoffs=1 steps=0  refused (write-path-unbuilt)
43284 needs-sa    ：refused (step-write-refused:call-failed/splice=-111)  ← 门/桥/镜像/对端皆过，停在发送（无 root ⇒ 建不了 SA）
43499 干净路径    ：verdict none
```

- `write-path-unbuilt` 是**说真话**的名字（此前写类能力错报 `device-absent`，真机上是假的）。
- 悬案结清：某支 fixture 早先报旧名，只因设备上那份是补 `required_param` 之前的拷贝；补推后即走到边界。
- 约定入档：拒绝名的三种细节来源（errno / 步骤码 / 状态机码）写进 `docs/development/introduction.md` 三·补。
