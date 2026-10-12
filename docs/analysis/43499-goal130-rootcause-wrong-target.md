# 43499 · 第 130 轮：**根因 —— 写目标是 `child_task->cred`，而不是 `init_cred + 8`** ✓✓✓

## 一、退役逐字（✓✓✓，`steps.cpp:165` ✓）

```cpp
// 退役：w2 的写请求
session, "W2: cred", **child_task + ghostlock::profile::task_cred_off()**, 2, …   // ★ 目标 = child 的 cred 字段 ✓
```

```
⇒ **⇒ 归档的 w2 目标 = `child_task + task_cred_off()`** ✓ = **victim 的 `task_struct` 里 `cred` 字段的地址** ✗
   ⇒ 而与归档日志一致 ✓：`target=0xffffff8a19880798` ✗（⇒ `child_task` 的别名视图 ✓，非 `init_cred` ✓）；
⇒ 本树（`:838`）：`kAliasBase + init_cred_image + alias_image_delta + 8U` ⇒ **`init_cred + 8`** ✗。
```

## 二、两者语义完全不同（✓✓✓）

```
| | 目标 | 效果 |
| --- | --- | --- |
| 归档 ✓ | `child_task->cred`（= `child_task + cred_off` ✓） | 把 victim 的 **cred 指针**指向 init_cred ⇒ **身份变 root** ✓ |
| 本树 ✗ | `init_cred + 8` | 改 **init_cred 内部**第 8 字节 ⇒ **对任何进程身份**无影响** ✗ |
⇒ ⇒ ⇒ **⇒ 这就是"机制读数全对、而 uid 恒为 2000"的根因** ✓✓✓：
   **写确实发生了（或本可发生）✗，但它改的不是"谁的 cred 指针"** ✓。
```

## 三、因此（✓，下一步唯一）

```
① 取 `task_cred_off()` 的权威值 ✓（⇒ 本树 abi ✓：`abi.task_struct.cred` ✓，我早在第 40 轮核过 ✓）；
② 取 **`child_task`**（= victim 的 `task_struct` 别名基址 ✓）⇒ ⇒ 本树**已有的**路径 ✓：
   `main.cpp:749 kAliasBase + init_task_image + alias_image_delta` ✓（⇒ 与"task 别名"同法 ✓），
   且本树**已知 victim task 地址** ✓（⇒ `perf_find_task` 的两次一致 ✓ ⇒ 就在 victim 侧 ✓）；
③ 于是修法 = **把 w2 的目标由 `init_cred + 8` 改为 `child_task + cred_off`** ✓（⇒ 与退役逐字同 ✓）；
判据：`main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
