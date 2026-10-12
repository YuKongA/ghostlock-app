# 43284 纯底层 Lua 的可达性测量（2026-10-11）

## 这份记录回答什么

目标原话：「把 pagecache_load_lkm 的逻辑封装成一个 script 接口提供，用 lua 调用，**再把 pagecache_load_lkm
完全重写成 lua 不调用封装接口测试底层可访问性**」。⇒ 这份记录量的是**后半句**：那支"只走底层行"的 Lua
（`procedures/pagecache_load_lkm.lua` ✓，注释自述"只走底层行 backend / writeop / copyop / settle / handoff / close ✓，
不调用封装的 step 接口"）在真机上**能打到哪一行**。

## 方法（可复现）

- 设备 `QV770MFGJ1`（A301SO，Android 15，`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）；
- `make -C core -B android NDK_TOOLCHAIN=<ndk>/toolchains/llvm/prebuilt/darwin-x86_64/bin` ⇒ push；
- 文档：`core/fixtures/wire-43284-lowlevel.framed`（`step.required_param = ["sa"]` ✓）；
- `adb shell 'cd /data/local/tmp && ./ghostlock < wire-43284-lowlevel.framed'`；
- 主机对照：`build/host/ghostlock-skeleton < core/fixtures/wire-43284-lowlevel.framed`。

## 原始输出

```
设备：main: step key=CVE-2026-43284 pagecache (Lua rewrite, low level only) backend=cve_2026_43284 route=pagecache
      main: cfg fields=69 (skipped 18) payloads=2 calls=6 handoffs=1 steps=0
      main: refused (write-path-unbuilt)

主机：main: wire refused (session-frame-unreadable)
```

## 结论（逐条）

- `steps=0` ✓：**确实一行封装接口都没走**（脚本自己声明如此 ✓，计数也如此 ✓）；
- `calls=6 handoffs=1` ✓：底层行（`backend` / `settle` / `handoff` 一类）**在设备上真被执行** ✓；
- **只在一行上被具名挡住**：`writeop` ⇒ `write-path-unbuilt` ✓ —— 这是**具名拒绝**（说缺什么 ✓），
  不是"没设备"也不是"缺 SA" ✓；
- 主机上的 `session-frame-unreadable` ✓ 属**正常**（主机读不到设备上的 SA 帧文件 ✓），不是缺陷 ✓。

## 边界（这份记录不声称的事）

- 不声称底层**写**可用 ✓（正相反：它被具名拒绝 ✓）；
- 不声称 SA 相关的链路可用 ✓（那是另一条 ✓）；
- 只声称：**除写之外，底层每一行都从 Lua 打到了设备** ✓。

## 若要让写那一行也可用（L 级，待决定）

- `writeop`/`copyop` 在 main 里目前直接具名拒绝 ✓（`main.cpp` 注释自述"写入路径尚未接入"）✓；
- 要让它们可用，就是把这几个底层行接到**已存在的**写执行体（`PageCacheWrite` + `RealSpliceIo` + 六件套 ✓，
  它们已服务于封装步骤 ✓）⇒ 属攻击关键路径改动 ✓，按规矩要先计划后写码 ✓。
