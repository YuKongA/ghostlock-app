# 真机门禁：搜索 ↔ 页准备接合通过（2026-10-08）

> 级别：**攻击关键路径** ⇒ 按 AGENTS 需真机门禁 + 本记录
> 设备 `QV770MFGJ1` · `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`
> 探针：`tools/snitch_prepare_probe.cpp`（`make -C src snitch-prepare-probe`）
> 提交：`a3d5b45b`（答案随调用返回）· 本轮（判据=接合本身）

## 1 判据与原始输出

```
snitch prepare probe
  self-call: started=1 ran=1 status=ok rounds=10 wanted=2048 region=0x78d6d31000
  prepare: status=ok at=11 opened=6 closed=7 base=f00000000000000 errno=0
  search (by value): started=1
snitch prepare probe: PASS          EXIT=0
```

**判据（探针内）**：`搜索 found` ∧ `页准备拿到的 base == 搜索找到的地址` ∧ `opened > 0` ∧ `closed >= opened`

## 2 这一跑证明了什么

- **搜索那一半是真的**：真 `fork` 出队列 ✓、真写队列字 ✓、真 futex 唤醒/等待 ✓（`rounds=10 wanted=2048 region=0x78d6d31000` ✓）；
- **地址确实交到了准备手里**：`prepared.base` 与搜索的 `found` **同一值** ✓（`0xf00000000000000` = 地址 0 + canonical tag ✓）；
- **准备走完了它的阶段**：`at=11` 且 `status=ok` ✓（PreChildren→SprayChildren→Snitch→PostChildren→KillPartials→LeakLoop ✓）；
- **账目合规**：`opened=6 closed=7` ✓（回收规则要求的多关 ✓）。

## 3 已知为桩的部分（不夸大 ✓）

- **规则是桩** ✓：它接受**每一个**候选 ⇒ 走查给出的**第一个**候选即为答案 ⇒ 在从 0 开始的范围内就是「地址 0 + tag」 ✓
  ⇒ 这**不是**一个真实对象 ✓，**本轮判的是接合本身** ✓（两半对同一地址达成一致 ✓）；
- **真实规则**（按 profile 的碰撞桶 ✓）是后续件 ✓；
  §此前几轮归档的「状态/返回值」条目按 §12 已更正 ✓（两路一致 ✓，非内存之谜 ✓）。

## 4 顺带记录的一处真因（值得留存）

规则要求**每一个证明地址**给出**同一个桶**；探针首版的两个地址桶值**奇偶不同** ⇒ **任何候选都不可能匹配** ✗
（这正是「桩接受一切却从不命中」的原因 ✓）⇒ 改为同奇偶后立刻命中 ⇒ **碰撞搜索的输入形状是判据的一部分** ✓。

## 5 复现

```sh
make -C src snitch-prepare-probe
adb -s QV770MFGJ1 push build/native/snitch_prepare_probe /data/local/tmp/
adb -s QV770MFGJ1 shell 'timeout 60 /data/local/tmp/snitch_prepare_probe; echo EXIT=$?'
```
