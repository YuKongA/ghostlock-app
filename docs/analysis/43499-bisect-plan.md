# 43499 · 引擎两相位已实现（round 77）与否定结果

## 实现（都在码里 + 都走 wire ✓）

1. `MulticastRouteConfig` 新增 `post_requeue_settle_us` / `post_adjust_settle_us`（退役同名 ✓）；
2. `requeue()` 返回后等第一个相位 ✓；consumer 每次 `setattr` 后等第二个 ✓；
3. `RouteConfigValues` 加两域 + `build_route_config` 只查范围（0 = 调用方明确说"不等" ✓）；
4. main 读 wire 键 `backend.cve_2026_43499.execution.race.post_requeue_settle_us` / `…post_adjust_settle_us` ✓；
5. 夹具填退役真值（200000 / 100000 ✓）。

## 真机结果（相位生效 ✓，但仍无 root ✗）

```
main: route status=0/0 attempts=2 calls=1 successes=1   ← calls 由 4 降为 1 ⇒ 相位确实在等 ✓
main: step result w1 => won / w2 => won / w3 => won
main: victim uid=2000                                  ← 仍未 root ✗
```

⇒ **又是一条否定结果**：相位对齐**不是**那个杠杆 ✓（但它现在是"已排除项" ✓，而不是"未验证" ✓）。

## 已排除项清单（本晚累计，全部真机验证过）

- 页面内容（字节级证对 ✓）、元组 `{pc=cred,right=0,left=target}` ✓、`prio=140` ✓、`list_link=fake_task|1` ✓；
- 凭据别名空间（已搬到 `0xffffffc0…` ✓）、三个 task 锚（`init_task`/`root_task_group` ✓）；
- 引擎契约、requeue 的 `nr_requeue`/`val3`（已与归档同形 `-1/EAGAIN` ✓）、`owner_started`；
- 三步全部执行 ✓、脚本顺序 ✓、队列 ✓；
- 相位对齐 ✗（本轮 ✓）；`consumer.delay_us=200000` ✗（上轮 ✓）。

## 下一个实验（能一刀切开两种可能 ✓）

把 `left`（写目标）**改成我们自己映射里的一个地址** ✓（其内核别名已知 ✓、内容可读回 ✓），然后跑：

- 若该处**变了** ⇒ 机制是通的 ✓ ⇒ 问题在"受害者地址/别名"上；
- 若**没变** ⇒ 机制本身没触发 ✓ ⇒ 问题在"内核那条走链"上。

⇒ 这是一个**二分**实验：不再靠对齐猜测 ✓，而是让数据说话 ✓。
