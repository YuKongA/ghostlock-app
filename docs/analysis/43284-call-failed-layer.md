# 具名链闭合：call-failed 就是 Access::write 的第五层（2026-10-15）

`call-failed` = `core/src/contract/kernel_access.hpp:73` 的 `Status::Failed`。
对照 `Access::write` 的判定顺序（本轮重读）：

```text
channel != PageCache             → Unsupported      （不是）
!params->usable_for_access()     → BadParameters    （不是）
!ops_->up()                      → NotInitialized   （不是）
in.size() != kWriteWidth         → BadWidth         （不是）
!ops_->land_block(offset, block) → Failed           ← 就是这里
```

⇒ 写已经走到 `land_block`，是**落地层那一步**失败 ⇒ 与 `43284-write-path-is-esp.md` 一致：
写要经 ESP 报文，报文要内核里那条 SA；探针的 sa 材料是随手造的，内核里没有对应的 SA ⇒ 必然失败。

真机链路（A301SO，APK 1010，逐层具名）：

```text
门 ✅ → 拼接面 ✅ → 落地 ✅ → Access ✅ → initialize ✅ → write → land_block ✗（缺 SA）
```

⇒ 「只缺那条 SA」从推断变成了**逐层证据**。下一步只需要一份可用的会话材料（帧）。