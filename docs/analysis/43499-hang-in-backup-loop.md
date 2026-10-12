# 43499 · 卡点收窄到"备份/安装循环"（round 216）✓✓

## 打点链条（本轮 ✓，逐字 ✓）

```
main: sel-branch enter ✓  → words filled ✓ → before execute ✓
main: sel-prep pipe ✓ → sel-prep timer ✓ → **sel-prep plan ✓ low=3** ✗ 之后 **EXIT=124** ✗
⇒ ⇒ 卡点在 `prepare()` 的**备份/安装循环**内（fd = plan 给出的 {0,1,2} ✓）
```

## 循环里只有两件事（✓）

```cpp
const std::int32_t copy = Ops::duplicate_above(fd, 64);   // fcntl(fd, F_DUPFD, 64) ✓
… if (Ops::install_at(high_read_, fd) != 0) { … }          // dup2(high_read_, fd) ✓
```

| # | 嫌疑 | 说明 |
| --- | --- | --- |
| 1 | `dup2(high_read_, 0/1/2)` 把**标准 fd** 换成 timerfd 的副本 | 换掉 0/1/2 会让后续 `printf` 走到别处 ✗（但不应**阻塞** ✗） |
| 2 | `fcntl(fd, F_DUPFD, 64)` | 理论上不阻塞 ✗ |
| 3 | `plan_.installed_at`（= 319 ✓）相关的 `FD_SET` 与安装位 | 与本循环相邻 ✓ |

## 本轮已就位（✓）

```
· `execute` 开头的**三字归零**（快照 PiRace 自带零初值 ✓；我用区域内存 ✗ ⇒ 同族隐患已消 ✓）；
· 逐 fd 打点（`sel-prep dup fd=%d` / `dup ok fd=%d copy=%d` ✓）⇒ 下一次 `accepts=1` 的跑即可定到**第几个 fd、哪一步** ✓。
```

## 下一批（✓）

```
① 抓一次 `accepts=1`（≈1/4 ✓，每批 4-6 跑 ✓）⇒ 由逐 fd 打点读出卡点 ✓；
② 若确为"换掉标准 fd"引起 ⇒ 按退役 `reserve_standard_io()` 的做法对齐 ✓（快照 :511 ✓）；
③ 之后回到 `value=0x4141…` ✗ 与 `buf=264` ✗。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
