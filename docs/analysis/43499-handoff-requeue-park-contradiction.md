# 43499 · 交接：requeue/park 的尖矛盾（2026-10-12）

## 一、已达成（真机证据）

```
· 标准 JSON ＋ lua 步骤 ＋ 真机执行（含 Lua 执行证据 ＋ 门禁归档）✓
· 「实现」：门面在真机接管 —— `main: access bound` ×15 ✓（⇒ window_attempt 走 WordLanding::land_word ✓）
· 按 ref 对齐并真机验证：cmp_requeue_pi 的 val3=0 ✓、val=1 ✓、五个 futex op 去掉 _PI_PRIVATE ✓
  ⇒ 效果：requeue 由 -1/EINVAL(22) 变为 **ret=0** ✓（本会话首次成功）
· 三条探针读数（真机 ✓）：
    main: park about-to (uaddr=wait_word value=0x0 uaddr2=target_word)   ×32
    main: park ret=66 errno=110 (ETIMEDOUT)                              ×32   ⇒ **确实挂起**
    main: requeue ret=0 errno=0                                          ×32   ⇒ **被接受但唤醒 0 个**
```

## 二、尖矛盾（下一步要解决的唯一问题）

```
同一字（`&shared_.wait_word`）：park 挂起 ✓、requeue 被接受 ✓ —— 却**没唤醒** ✗。
⇒ 唯一自洽：**两侧用的不是同一个 futex 对象** ✗（而源码两边都写 &shared_.wait_word ✗）。
```

## 三、已证伪的假设（不要重复）

```
· owner 置位次序（第 261 轮改动 ⇒ 真机读数逐字不变 ✗）
· 门控忙等（第 259 轮改短睡 ⇒ 读数逐字不变 ✗）
· val3 / val 的取值（已按 ref 对齐 ✓ ⇒ 带来 ret=0 的成功 ✓）
· expected 写死 0（本树注释逐字：写死 0 ⇒ 等待者**根本没睡**，真机量到过 ✗ ⇒ 不改 ✓）
· 三种 uaddr/uaddr2 组合（11 ✗／22 ✗／11 ✗，矩阵已量 ✓）
```

## 四、下一步（两选一，都可执行）

```
① **实测两侧的真实实参**：在 WAIT 与 REQUEUE 两处各打印 `wait_word` 的**地址**（`&shared_.wait_word`），
   以及 `uaddr2` 的地址 ⇒ 一次读数即可判定"是否同一个对象" ✓
   落点：multicast_route.hpp 的 park 处（已有一条 about-to 读数 ✓）与 requeue 处（`shared_.requeue_result = …`）
② 按用户指示做 **cmp_disasm**：
   `python3 tools/cmp_disasm.py build/native/ghostlock-B0 build/native/ghostlock`（introduction.md:44 ✓）
   ⇒ 需要先产出一个 **-B0 基线**（如"回退到本批改动前"的构建 ✓），再与本构建比对 ✓
```

## 五、交接要点（易错处）

```
· `MulticastRouteConfig` **无** `state_poll_interval_us` 字段（我试过 ⇒ 编译错 ⇒ 已回落 ✓）；
· `shared_.wait_word` 是**普通** `std::uint32_t`（⇒ 没有 `.load()` ✗ —— 我犯过 ✓）；
· `cve49` 别名只在 `#ifdef __ANDROID__` 内可用（⇒ host 编译不可用 ✗ —— 我犯过 ✓）；
· 三线门禁：`make -C core -j1 check`（109 PASS 起）／`make -C core -B android`（零告警）／`./gradlew markdownlint`；
· 真机：`adb push build/native/ghostlock /data/local/tmp/` ＋ `./ghostlock < lua-steps.framed`（约 8 分钟/次）。
```
