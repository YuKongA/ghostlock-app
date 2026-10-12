# 43499 · `select_stack` 已接线并在真机跑起来（round 182）✓

## 接线（计划第 4 步 ✓）

```
main.cpp 的路由分支：route == "select_stack" ⇒
  由文档读 12 个 waiter 偏移 ✓ ⇒ fill_select_stack_words 换算词号 ✓（÷8 ✓）
  attempts / fake_task / fake_lock / target / value 全由文档与既有路线给 ✓
  select_route.execute(chain_word, target_word, {0,1,2}, 3) ✓ ⇒ 打印 select route 报告 ✓
```

## 真机结果（5 跑；只有 1 跑过了奇偶门 ✓）

```
run1: main: select route status=0 step=61 attempts=128 calls=128 successes=0
      refusal=select-stack-no-consumer-success
run2-5: refused (payload-layout-not-accepted)   ← 奇偶门（先于路由选择 ✗）
```

## 这个结果说明什么（硬结论 ✓）

| 事实 | 含义 |
| --- | --- |
| route **跑起来了** ✓（128 轮 ✓） | 移位/词表/资源帧/循环/演员**全部工作** ✓；选择器接线正确 ✓ |
| `successes=0` ✗（真判据 `Running` 未得 ✓） | 内核**没有搬动等待者** ⇒ 该 route 的**形状/词表/时序**仍需对 ✓ |
| 与 multicast 对照 | multicast 报 `waiter=-1` ✗ 是**同一个问题** ✓ ⇒ 两条 route 现在有**同一把尺** ✓ |

## 意义（诚实 ✓）

- 这不是"成功" ✗，但它把"换 route"从**计划**变成了**可迭代的真机实验** ✓；
- 且判据是**内核侧的**（`cmp_requeue_pi >= 1` ⟺ 搬过 ✓）⇒ 下一步的每次调整都能立刻判定 ✓；
- 奇偶门（先于路由选择 ✗）导致 5 跑只有 1 跑到达 route ✓ ⇒ 后续判据一律**多次统计** ✓。

## 门禁（本轮 ✓）

```
check → RC=0，108 PASS ✓；android → RC=0，0 error / 0 warning ✓；lint → 0 ✓
```
