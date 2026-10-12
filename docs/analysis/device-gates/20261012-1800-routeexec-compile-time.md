# 设备门禁：RouteExec 编译期改造（2026-10-12）

## 目的

用户裁定「arm/execute 我怎么知道在不在 PI 内，不确定就改成编译期」——不去证 PI 边界，
直接把虚分派改成编译期，使两种情形都安全。本件记录该改动（记作 4）的攻击路径真机门禁。

## 改动的形状与判据

```
seam::RouteExec 由抽象基类改为 concept（multicast_window.hpp:42-43）
MulticastWindow 改为 template <RouteExec Exec>（:60）
窗口/access/attempt/context/binder 全部以具体路由类型参数化，调用在编译期
multicast_route_linux.cpp 增 static_assert(RouteExec<...>)，编译期对接判据

可失败判据：grep -c virtual core/src/backend/cve_2026_43499/seam/multicast_window.hpp
  改前 = 3（析构 + execute + arm），改后 = 0
并纳入 check：输出两行
  check: virtual is confined to the access interface (tests exempt)
  check: race window and the multicast window header carry no allocation, logging or indirect dispatch
```

## 三线门禁（最终树）

```
make -C core -j1 check                 EXIT=0   PASS=109   FAIL=0
make -C core -B android NDK_TOOLCHAIN=  EXIT=0   error=0    warning=0   产物 5567720 B
./gradlew markdownlint                  EXIT=0   601 files  0 issues
```

## 真机读数（adb -s QV770MFGJ1，A301SO / Android 15）

```
热机 17:55 与冷机 17:58（adb reboot，等 sys.boot_completed=1）各一次，同夹具 lua-steps.framed（12609 B）
推包前已 make -C core -B android

项                          热    冷    基线  判读
rc                          9     9     9    相同
main: refused (param-invalid)  0   0     0    相同
main: step result w1 => won  1     1     1    相同
value=9（脚本 surface.log）   1     1     1    相同，逐字 [lua] surface -> call index value=9
main: victim uid=2000       16    16    16    相同
main: victim uid=0           0     0     0    相同（本批不改变该结果）
access bound route=multicast_waiter seam=1  16  16  16  门面接管，fallback=0
window clean=1 violations   alloc=0 log=0 indirect=0 lua=0   四项窗口违规全 0
前置：/data/adb/modules 空、无 /data/adb/ksu  => KernelSU 未加载
```

## 未验证项

```
1. 真机只跑一次冷机，未做多次冷机复跑（时序类判据按 AGENTS 8 需同构建多次复跑才可归因）。
2. uid=0 仍未取得：真机 uid=0 计数为 0，w2 => w2-not-rooted，与基线同。
3. stdout 字节数与 1438 那份不同源（54627/52680 对 58509），原因：那份用的 framed 是 12087 B，
   而工作树里的已变为 12609 B。本件两次同夹具，读数逐项一致。
```

## 附：本轮并行核实的一条非回归

```
make -C core host  =>  EXIT=2
  host: running the skeleton
  main: wire refused (wire-no-frame)      源：wire_doc.cpp:160
判读：该目标跑 skeleton 并把 stdin 当 framed 文档读；无输入 => 无帧 => 具名拒绝。
      不是本次任何改动引入的回归；属既有设计债（无输入时该目标必然失败），不在本批门禁清单。
```
