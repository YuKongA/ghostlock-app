# 43499 · 第 70 轮：**生产路径缺 CPU 绑定**（与归档同源差异，且无需 app）✓✓✓

## 一、读数（✓✓，逐处 ✓）

```
本树：`grep pin_to_processor|sched_setaffinity|…` ⇒ 命中**只在** `tools/*`（探针 ✓）与 landing 的**定义** ✓
      ⇒ **main.cpp / route / steps 的生产路径里没有绑定调用** ✗；
归档（逐字 ✓）：`cpu pair: main=0 consumer=1` ✓ / `recommended_cpus.main=0 consumer=1` ✓
                / `selected_cpus.main=0 consumer=1` ✓ ⇒ ⇒ **生产路径有绑定** ✓。
⇒ ⇒ **⇒ 我树缺这一步** ✗ ⇒ ⇒ 抢占/迁移行为与归档**不同** ✓。
```

## 二、为什么这与本路线的机制直接相关（✓✓）

```
本路线的核心是"**拷贝落在调用者的栈上**"✗ —— 一个**依赖抢占/时序**的事件 ✓；
退役也据此写 `arm_hold`（20000 次 yield ✓）：⇒ **yield 计数只有在"抢占可预测"时才有意义** ✓；
⇒ ⇒ 若线程会在 CPU 间迁移 ✗ ⇒ 那段自旋的时间尺度完全不同 ✓ ⇒ ⇒ 落点概率随之变化 ✓。
⇒ ⇒ ⇒ 这与第 67 轮的**域假设同源** ✓（⇒ 都指向"调度/抢占条件"✓），但**无需 app** ✓✓。
```

## 三、下一步（✓，一处小改即可测 ✓）

```
① 在 main 的会话启动处，按文档/归档同形绑定：main → CPU 0 ✓、consumer → CPU 1 ✓
   （键位与数值来源：`recommended_cpus.*` / `selected_cpus.*` ✓，见归档 ✓；本树若已有该键 ⇒ 直接用 ✓）；
② 绑定后用既有的真机判据复跑 ✓：`victim uid` ✓ / `clean=1/1` ✓ / `stamp=-1/99` ✓ 逐项对照 ✓；
③ 判据：任一轮 `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
