# 43499 · `accepts=1` 的跑卡在 select 分支（round 214）✓

## 硬事实（本轮 ✓）

```
1. `setvbuf(_IONBF)` **本来就在** ✓（main.cpp:1542 ✓，注释还写着"真机踩过" ✓）⇒ 缓冲**不是**尾丢失的原因 ✗；
2. 无缓冲的日志**完整**且停在：
     main: rv … target=0xffffff88… value=0x4141414141414141 ft=… fl=… **buf=264** ✗
   ⇒ `buffer_size = 264` ✗（而 prepare 日志说 `region=8328 bytes` ✓ ⇒ 量级不符 ✗）；
3. 该跑 `EXIT=**124**` ✓ = 被 `timeout` 杀掉 ⇒ **是卡，不是崩** ✗ ✓；
4. 卡点在 `build_route_config`（纯校验 ✓）之后的 **select 分支**（我的新代码 ✓）。
```

## 由此确定的两件事（✓）

| # | 结论 | 依据 |
| --- | --- | --- |
| 1 | 门/奇偶**不是**拦路虎 ✓（accepts=1 ✓ 走到很后面 ✓） | 本轮与上轮读数 ✓ |
| 2 | 当前拦路虎 = **select 分支内的卡** ✗ | `EXIT=124` ✓ |

## 下一步（下一批 ✓，先定位再改 ✓）

在 select 分支里加**相位打点**（一进分支 / 填词号后 / execute 前 / execute 内每阶段 ✓）⇒ 一次跑即可定位卡点 ✓。

做法：打点位置为「一进分支 / 填词号后 / execute 前 / execute 内每阶段」⇒ 一次跑即可定位 ✓。

候选如下（按可能性 ✓）：

```
`fill_select_stack_words`（纯 ✓ 不会卡 ✗）/ `SelectStackRoute::prepare`（真 syscall ✓）/
`run_actors`（线程 ✓）/ `pselect`（有界 250 ms ✓）/ `stop_actors`（有上限 ✓）/ `race_.run`（真 futex ✓）
```

另注：`buf=264` ✗ 是**另一条独立线索**（几何量级不对 ✓）⇒ 定位卡点后一并查 ✓。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
