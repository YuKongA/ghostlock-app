# 真机门禁：准备阶段描述符归还（2026-10-10）

> 级别：**攻击关键路径改动** ⇒ 按 AGENTS §8 需真机门禁 + 本记录 ✓
> 设备 `QV770MFGJ1`（A301SO · Android 15 · `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）
> 代码：`core/src/backend/cve_2026_43499/memory/prepare_page.hpp`（早退路径的归还 + 逐组计数）、
> `core/src/tests/prepare_page_test.cpp`（逐组平衡的断言）、`core/src/main.cpp`（报告行）

## 1 症状（设备实测，先于任何改动）

```
四组各 1 ：prepare status=plan-missing step=7 opened=4 closed=3
原组     ：prepare status=plan-missing step=7 opened=5 closed=3
```

`opened` 随 spray 组数增长，`closed` 恒为 3 —— 差额与组数同步，说明有描述符没归还，但**它不说是谁**。

## 2 定位过程（含两次被否证的假设，照实记）

1. 假设「最后一个 post 描述符没归还」⇒ 改后设备数字**不动** ⇒ 撤回（当时判断：证据不足）。
2. 假设「被拒的开也被计入 opened」⇒ 主机测试证实该行为**存在**（`fake` 返回 `-1` 时 `opened>closed`），
   但设备上每次开都成功 ⇒ 数字**不动** ⇒ 该修复作为计数正确性保留（设备无影响），假设本身被否证。
3. 真正生效的一步：给 `PrepareOutcome` 加**逐组**开关计数（纯报告字段）⇒ 差额自己报出组名：

```
四组各 1 ：open=1/1/1/1 close=1/1/0 (pre/spray/post/leak)
原组     ：open=1/2/1/1 close=1/1/0
```

⇒ **post 组开 1 关 0**；组数增长后又暴露 **spray 组开 2 关 1**。

## 3 改动

- `close_them_all` 关掉一个描述符后把它在 `PrepareState` 里标 `-1`（早退路径不会二次关闭）。
- 早退路径（`LeakLoop` 无碰撞）在归还后**遍历所有仍未关闭的描述符**逐一归还并计数（pre/spray/post），
  最后关 leak 描述符 —— 顺序不变（leak 仍是最后一个关闭的那一个）。
- 主机测试新增「无碰撞时，凡是开过就必须归还，且逐组自平」的断言（组数 pre2/spray4/post2/leak1，
  即覆盖了设备暴露的 spray>1 情形）。

## 4 改后真机结果（`make -B android` 全量重建后推送）

```
四组各 1 ：opened=4 closed=4   groups open=1/1/1/1 close=1/1/1
原组     ：opened=5 closed=5   groups open=1/2/1/1 close=1/2/1
```

⇒ 两个用例都平衡，逐组自平。

## 5 教训（本批最重要的一条）

`prepare_page.hpp` 是**头文件**；上一批改动没有触发依赖重编，**设备一直在跑旧对象文件**，
导致连续两轮结论失真。**凡真机验证前一律 `make -B android`**（AGENTS §8 本就如此要求）。

## 6 复现命令

```sh
make -C core -B android NDK_TOOLCHAIN=$HOME/Library/Android/sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin
adb -s QV770MFGJ1 push build/native/ghostlock /data/local/tmp/
adb -s QV770MFGJ1 push core/fixtures/wire-43499-window-{full,groups1}.framed /data/local/tmp/
adb -s QV770MFGJ1 shell 'cd /data/local/tmp && ./ghostlock < wire-43499-window-full.framed'
make -C core -j1 check && ./gradlew markdownlint
```

## 7 未决（同批登记）

- `prepare_page.hpp` 里整形段那处 `++outcome.opened` 是**有条件**的（`if (state.leak_fd >= 0)`），
  语义未确认（疑似把回收 socket 对算作一次开），本批**未碰**。
- 主机测试套件**并行跑会误报**（`multicast_route_test` 在 `make -j` 下稳定失败，串行/单独跑均通过）。
  本批起门禁一律 `-j1`。
