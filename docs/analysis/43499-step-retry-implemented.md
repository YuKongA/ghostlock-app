# 43499 · 步骤级重试已实现并跑起来（新目标第 2 轮）✓✓

## 本轮实现（✓，与退役 `retry_write_stage` 同义 ✓）

```
在 `on_op` 的 step 分支（main.cpp:1391 ✓）把"跑一次"包成**重试循环** ✓：
· 轮数/间隔取自 wire：`execution.stages.<step>_attempts` / `_settle_us` ✓（不写死数字 ✗）；
· 判据：`w2` ⇒ **受害者自报 uid==0** ✓（= 写真的落到 cred 字段 ✓，`victim_uid` 初值 0xFFFFFFFF ✓）；
· 每轮打印 `main: step retry <step> round=N of=M uid=0x…` ✓（重试过程可见 ✓）。
```

## 真机读数（✓）

```
main: step retry w1 round=1..4 of=15 uid=0xffffffff   ⇒ 第 5 轮过 ✓ ⇒ main: step result w1 => won ✓
main: step retry w2 round=1..7 of=15 uid=0xffffffff   ⇒ 运行中（被本轮的 180 s 上限截断 ✗）
⇒ ⇒ **重试机制本身工作正常** ✓；`w2` 连试未过 ✗ ⇒ 与"cred 写不落地"一致 ✓。
```

## 同批修掉的测试（✓，语义随目标更新 ✓）

```
`route_config_test.cpp` 原本把"value==0"当作**必须被拒** ✗ ⇒ 与新语义冲突 ⇒ 更新 ✓：
· 从"清零即拒"的表里移除 `value` ✓（附理由注释 ✓）；
· 新增 `zero_value_is_accepted()` ✓（零值被接受且抵达 config ✓）并在 main 里调用 ✓；
⇒ `check` 回绿 ✓（RC=0，108 PASS ✓）。
```

## 下一步（✓，后台跑已启动 ✓）

```
① 完整 15 轮跑（后台 ✓）⇒ 判断"写是否**概率性**落地"（某轮 uid==0 ✓）还是**系统性不落** ✗；
② 若系统性 ⇒ 对照退役的 `route_middleware`/`stage_types` 看 W2 的**目标/值/页**是否还有一处不同 ✓。
```
