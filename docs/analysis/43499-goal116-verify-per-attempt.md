# 43499 · 第 116 轮：**结构对上 —— 退役的 uid 读在「每次尝试后的 verify」里** ✓✓✓

## 一、退役逐字（✓✓✓，`victim_process.cpp` 的 `verify_w2_stage` ✓）

```cpp
int32_t verify_w2_stage(void *context) {                        // ★ **w2 阶段的验证回调** ✓
    auto *stage = static_cast<struct w2_stage_context *>(context);
    if (write(stage->pipes.cmd_write.get(), "C", 1) != 1) return 0;        // 发 "C" ⇒ 要 uid ✓
    uint32_t child_uid = 9999;
    if (read(stage->pipes.uid_read.get(), &child_uid, sizeof(child_uid)) != sizeof(child_uid)) return 0;
    pr_info("child uid = %u\n", child_uid);                              // ← 归档那行 ✓
    if (child_uid != 0) return 0;                                        // 非 0 ⇒ 该次**失败** ✓
    pr_success("child is root!\n");                                     // ← 归档那行 ✓
    return 1;
}
```

## 二、它与循环的关系（✓✓✓，第 96 轮逐字对照 ✓）

```
退役 `retry_write_stage`：`for (attempt…) { …attack_write…; if (verify(context)) return true; }` ✓
⇒ ⇒ **⇒ 即：*每次尝试之后*都调用 `verify_w2_stage`** ✓ ⇒ **⇒ 每次尝试之后都读一次 child 的 uid** ✓；
⇒ 而归档里 `child uid = 0` 出现的位置（`:142` ✓，紧接 `W2b: firing prebuilt init_cred+8 repair` 之后的
   第三次路由 ✓）⇒ ⇒ **⇒ 说明那是"修复路由那次尝试之后"的验证读数** ✓✓。
```

## 三、本树的差异（✗✓，最后一块拼图 ✓）

```
本树：`main.cpp:1378-1383` 的读 uid 在**步骤循环之外** ✗（⇒ 步骤跑完才读一次 ✓）；
⇒ ⇒ **⇒ 于是"修复路由之后有没有*再验一次*"** ✗ 就成了关键 ✓：
  ① 若修复路由**之后**会再发 "C" 读 uid ⇒ 那才是与归档同形的判据 ✓；
  ② 本树是否如此 ✗ ⇒ 需一处读（`main.cpp:1370-1385` ✓）⇒ **⇒ 下一步唯一要看的** ✓；
⇒ ⇒ ⇒ **⇒ 若是"步骤跑完才读"** ✗ ⇒ ⇒ **⇒ 那本会话的"机制全对而 uid 不动"就有了最后解释** ✓✓：
   **写发生在修复路由里 ✓，而读数发生在*那之后*的步骤级检查** ✓ ⇒ 二者**时机/对象**可能已错位 ✗。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
