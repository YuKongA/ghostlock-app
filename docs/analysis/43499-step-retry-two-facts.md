# 43499 · 步骤级重试：已实现；两条硬读数（新目标第 2 轮）✓✓

## 实现（✓，与退役 `retry_write_stage` 同义 ✓）

```
main.cpp:1391 起：step 分支的"跑一次"包成**重试循环** ✓
· 轮数/间隔取自 wire（`execution.stages.<step>_attempts` / `_settle_us` ✓，不写死 ✓）；
· 判据：`w2` ⇒ **受害者自报 uid==0** ✓（`victim_uid` 初值 0xFFFFFFFF ✓ ⇒ 判据干净 ✓）；
· 每轮打印 `main: step retry <step> round=N of=M uid=0x…` ✓。
· 同批：`route_config_test.cpp` 随语义更新（零值合法 ✓ + 新增用例 ✓）⇒ check 回绿 ✓（108 PASS ✓）。
```

## 两条硬读数（✓，本轮 15 轮满跑）

```
① `w2` round=1..15 **全部 uid=0xffffffff** ✗ ⇒ **写是系统性不落**，不是抽样运气 ✓；
② 末轮 `step result w2 => victim-fork-failed` ✗ ⇒ **每轮重跑准备 ⇒ 资源耗尽** ✓：
   （本树每轮 `run_wire_step` 都整跑 prepare/spray ✗；退役的 `retry_write_stage` 是在**已准备状态内**重试 ✓。）
⇒ ⇒ 因此：① 指向"写路径本身"仍有差异 ✗；② 是**我实现层面的代价问题** ✓。
```

## 本轮的对应处置（✓）

```
把夹具 `stages` 调成不耗尽的规模（w1=8 / w2=5 / w3=3 ✓，仍走同一条重试代码路径 ✓）⇒ 让整链能跑完 ✓；
随后启动**后台完整跑** ⇒ 观察 w2 之后的 w3 与收尾（handoff ✓）行为 ✓。
```

## 下一步（✓）

```
① 完整跑的读数：`w3` 判据（seccomp 探针 ✓）是否可达 ✓、收尾是否发生 ✓；
② 把重试下沉到"准备之后"（⇒ 避免每轮重跑 prepare ✗）⇒ 才能用回退役的 15 轮规模 ✓；
③ 继续追"写为何系统性不落"（⇒ 对照退役 route_middleware/stage_types 的 W2 路径 ✓）。
```
