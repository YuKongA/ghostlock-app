# 事故恢复完成 + plan-check 的真因（2026-10-11 下午）

## 一、事故恢复：**构建目标全部恢复**

```
make -C core android   → ANDROID_RC=0，0 error/warning，产出 build/native/ghostlock (1443320 B) ✓
make -C core -j1 check → 107 PASS（唯一红见下）
./gradlew markdownlint → 0
```

### 恢复过程中修好的点

1. `RUNTIME_SRCS`（按早前日志的完整链接命令行逐字重建，31 源）；
2. `EVIDENCE_BINS +=` 六项 + 两个新测试；`check` 配方的逐测试调用行；补充测试的统一规则（`TEST_SRCS`）；
3. `SKELETON_SCRIPT_SRCS` 用 `=` 而非 `:=`（`SCRIPT_SRCS` 在 1124 行才定义 ⇒ 立即展开会得空）；
4. 主机/设备配方的 `-I$(LUA_DIR)`、`$(LUA_CORE_LIB)`、`$(DEVICE_EXTRA_SRCS)`；
5. `ANDROID_ONLY_SRCS` 按事故前 17 个单元恢复；
6. **新增设备 lua 核心库** `liblua-core-android.a`（22 个核心文件用 NDK 逐个编 + llvm-ar 成库，与 `mpack-android.o` 同款）。

## 二、`plan-check` 的真因：**不是事故**，是功能被删

- 现象：`plan-check` 把 **msgpack 的 profile 文档**按 4 字节长度前缀喂给骨架，判据是输出里要有 `queue-removed`；
  现在输出是 `main: wire refused (json-bad-number)`。
- 根因（代码原文）：`main` 里 `static_cast<void>(argc); static_cast<void>(argv);` ⇒ **argv 被显式忽略** ⇒
  一切按 `script::WireDoc::read_framed`（JSON wire）读 ⇒ 把 profile 文档当 wire ⇒ 坏数字。
- 时间线互证：早前日志（r229）里 `plan-check: PASS`，当时 main 还认那个参数 ⇒ **是本会话某次 main 改动删掉的**，
  与本次 Makefile 事故无关。
- 修法（属目标本身的工作）：补回"应用调用"分支 —— 读 **profile 文档**（msgpack，用 `glkv3_document`）、
  跑**计划闸门**（`pipeline/plan_gate.hpp`，其 `queue-removed` 正是判据）；这同时就是目标里
  "把 pagecache_load_lkm 的逻辑改成符合 schema 的配置传入执行"的那条路。

## 三、备份与无损

- `/tmp/Makefile.step1..21.bak`（21 份逐步备份）；**源码与测试全部完好**；设备二进制已按当前源重新构建。

## 四、恢复完成（2026-10-11 夜）

- `make -C core -j1 check` → **RC=0**（含 `plan-check: PASS` ✓）；
- `make -C core -B android` → RC=0，0 error/warning；
- `./gradlew markdownlint` → 0；
- 收尾两处真因：① 我补的应用调用分支的三个 include **落在了 `#ifdef __ANDROID__` 里面** ✗ ⇒ 主机看不到类型；
  ② `HOST_ALL_SRCS` 用 `$(sort …)` 去重（两组列表本就重叠）⇒ 29 个重复符号消失。
- 教训补充：**include 的位置也是语义**（放进条件编译块里，等于在另一个平台不存在）。
