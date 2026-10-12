# 43499 · 根因：零写值 0 被校验器误判为"缺失"（round 251）✓✓✓

## 真机读数是关键（✓，此前被我读漏 ✗）

```
main: step result **w1 => won** ✓
main: step result **w2 => won** ✓        ← 前两步**真的写成了** ✓
main: step result **w3 => route-config-missing:value** ✗   ← 链子断在这里 ✓
· 通过那次的 `steps=**3**` ✓（三步都跑了 ✓；我此前看的 `steps=1` ✗ 是**标签行硬编码** ✗ main.cpp:1764 ✓）；
· 与归档成功记录对照：它跑的正是 **W3: TIF_SECCOMP** 与 **W3: seccomp mode** ✓ = **w3 的两半** ✓。
```

## 根因（✓，代码依据 ✓）

```cpp
// main.cpp:750-753（w3 的请求）
page_request.target = spawned.task;
page_request.mode   = WriteMode::**Zero**;      // ★ 零写 ⇒ 写值合法性上就是 0
page_request.preserve_child = false;
// backend/…/steps/route_config.cpp: 逐个字段校验
if (!needed(values.value, "value", out)) { return out; }   // ✗ value == 0 被判"缺失"
```

⇒ ⇒ **零写（Zero 模式）的写值本来就是 0** ✗ ⇒ 而校验器把 `0` 与"没给"混为一谈 ✗ ⇒
   于是 `w3` 永远拿不到 config ⇒ 报 `route-config-missing:value` ✗ ⇒ **链子断在最后一步** ✓。

## 为什么这解释了整晚的现象（✓）

| 现象 | 解释 |
| --- | --- |
| `enforce` 恒 1 ✗ | **`w1`/`w2` 的写不带 seccomp 位** ✓ ⇒ 只有 `w3` 才动 TIF_SECCOMP/seccomp ✓ |
| `victim uid=2000` ✗ | 同上：拿 root 的链子在 `w3`（seccomp 解除）✓ |
| 归档成功那次 `enforce` **也是 1** ✗ | 一致 ✓ ⇒ 我此前的判据是**伪判据** ✗（已更正在前 ✓） |

## 修法（L 级 ⇒ 先出计划 ✓）

```
① `needed()` 对 `value` 放宽：`Zero` 模式允许 0 ✓（或在 w3 分支显式传一个"已给定"标志 ✓）；
② 同步检查其它"合法为 0"的字段（`target` ✓ 位移类 ✓ —— 注释里本就写着"位移可以合法地为 0" ✓）；
③ 改后真机判据：`main: step result w3 => won` ✓ ⇒ 再看 `main: victim uid=0` ✓。
```
