# 附件：判据的过滤规则（可执行形式）· 43499 日志点设计

> 分析类（≤8 KiB）。**从主件 [43499-logging-points-design.md](43499-logging-points-design.md) §8 迁出**（配额纪律：单份 ≤8 KB）。
> 规则一句话：**判据只认 native 行** —— 先剔出带来源标记的行，再计数。

## A. 规则与来源标记

- 标记由**日志接缝在 Lua 路径上**添加（`core/src/script/sandbox.cpp:438` 的 `surface_call`），**逐字 = 方括号内三个小写字母 lua**（方括号为字面字符，不是字符类）⇒ **脚本不可抑制、不可伪造**。
- **两条判据都要做**：① 剔出标记行之后数目标键；② 数标记行本身（对照：脚本确实跑到这里）。
- **为什么**：脚本能写出一行看起来像判据的文字；若不剔出 ⇒ 「一条 grep 定案」（C3）与「与代码具名串逐字相同」（C5）**失去证据价值**。

## B. 掷行命令（可直接粘贴）

```sh
# 计数与对照：native_lines / marker_lines / native_refusals / marked_refusals
python3 tools/lua_marker_guard.py <日志>

# 判据 A：剔出标记行之后数目标键（判据通路只喂 native 行）
python3 tools/lua_marker_guard.py <日志> --emit-filtered | grep -c '<键>'

# 判据 B（对照）：同一次输出里的 marker_lines（脚本确实跑到这里）

# 证伪：把过滤关掉指向同一份含伪拒绝行的日志 ⇒ 守卫必须失败
python3 tools/lua_marker_guard.py <伪造日志> --filter off
```

- **返回值语义**：`0` = 通过；**`2` = 失败**（标记行混入判据计数）；`1` = 用法或读取错误。

## C. 拒绝码表（**不新造**）

- 守衡的拒绝码集**解析自唯一权威** `core/src/pipeline/step_refusal.hpp` 的 `step_refusal_name()`（`case StepRefusal::X: return "…";`，**跳过 `Ok`**）⇒ 实测 **19 个码**（含 `target-outside-direct-map` · `w2-not-rooted`）；该头文件**只读**。
- 若头文件不可读 ⇒ 守衡回退到**同一份显式清单**（同样注明来源）。

## D. 自检与真机样本（原始输出）

```
$ python3 tools/lua_marker_guard.py /tmp/lua-guard-check/forged.log --assert-excluded
lua-marker-guard: refusals=19 source=core/src/pipeline/step_refusal.hpp
lua-marker-guard: file=/tmp/lua-guard-check/forged.log lines=3 filter=on
lua-marker-guard: native_lines=2 marker_lines=1
lua-marker-guard: native_refusals=0 marked_refusals=1
PASS: every marked refusal line was excluded from the judging count      ⇒ EXIT=0

$ python3 tools/lua_marker_guard.py /tmp/lua-guard-check/forged.log --filter off
lua-marker-guard: native_lines=2 marker_lines=1
lua-marker-guard: native_refusals=0 marked_refusals=1
FAIL: a marked line reached the judging count (1 marked refusal line(s))  ⇒ EXIT=2

$ python3 tools/lua_marker_guard.py /tmp/lua-guard-check/native-only.log
lua-marker-guard: native_lines=3 marker_lines=0
lua-marker-guard: native_refusals=1 marked_refusals=0                     ⇒ EXIT=0

$ adb -s QV770MFGJ1 pull /data/local/tmp/ghostlock.log /tmp/real.log
/data/local/tmp/ghostlock.log: 1 file pulled, 0 skipped. 21.0 MB/s (255566 bytes)
$ python3 tools/lua_marker_guard.py /tmp/real.log
lua-marker-guard: refusals=19 source=core/src/pipeline/step_refusal.hpp
lua-marker-guard: file=/tmp/real.log lines=3895 filter=on
lua-marker-guard: native_lines=3889 marker_lines=6
lua-marker-guard: native_refusals=0 marked_refusals=0                     ⇒ EXIT=0
```

- **真机口径核对**：`marker_lines = 6` ✓（本会话脚本发出的 surface 调用数）· `native_lines = 3889` ≈ 3890 ✓ · **6 + 3889 = 3895 = 总行数** ✓ ⇒ **格式假设成立，未改 MARKED 模式**。
