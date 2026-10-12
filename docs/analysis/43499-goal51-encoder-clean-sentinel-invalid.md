# 43499 · 第 51 轮：编码链干净 + 哨兵读数无效（再次自我更正）✓✓

## 一、编码链（✓，逐环 ✓）

```cpp
// write_encoder.cpp:20
WriteArm arm_for(const WriteRequest &request, std::uint64_t value, std::size_t pointer_bytes) {
    if (request.preserve_child) { arm.pc = value; arm.left = request.target; return arm; }   // ★ preserve 臂 ✓
    arm.pc = request.target >= backstep ? request.target - backstep : 0U; arm.left = 0U;      // leaf 臂 ✓
}
// write_encoder.cpp:44  encode_waiter_arm ⇒ put_word(fragment, offsets.pc, arm.pc, pointer_bytes) ✓
//   （同样写 right / left ✓；三者都先 fits() 检查 ✓）
⇒ 从"请求"到"页中三字"的**每一环都正确** ✓（且 w2 在第 34 轮已回退为 `preserve_child = true` ✓）。
```

## 二、哨兵读数为何不可用（✓✓，收回第 46/50 轮推论 ✓）

```
`sentinel-before` 的取值点在**建页之前** ✗（对比的是 `page` 缓冲当时的字节 ✓）；
而 `sentinel-after2` 打印的是 **`report.sentinel_before[…]`** ✓（`main.cpp:1165-1166` 逐字 ✓）
⇒ ⇒ **⇒ 它从未在"建页之后"读过** ✗ ⇒ 那些数字（491776 / 718974344）是**那时的残留内容** ✗，
   与"pc/left 是否写对"**无关** ✓ ⇒ ⇒ 第 46 轮"编码期结论"与第 50 轮"写了偏移"的说法**一并收回** ✓。
```

## 三、当前真正的状态（✓✓，干净表述 ✓）

```
已知**正确**（逐处核对 ✓）：写值 = init_cred 别名（非零 ✓）/ 目标同窗 ✓ / 页被采纳 ✓（`accepts=1` ✓）
  / 编码链（arm_for → put_word → 偏移）✓ / 路由 `successes=1` ✓；
唯一**未知**：**内核到底有没有把值写到 target** ✗ ⇒ ⇒ 需要在**建页之后、路由之后**各取一次**真实内核侧**读数 ✓
  （⇒ 现有哨兵读的是用户态缓冲 ✗，且时机在建页前 ✗ ⇒ 两者都不满足 ✓）。
```

## 四、下一步（✓，两小步即可 ✓）

```
① **零成本改一处**：把 sentinel 的基址从 `page`（用户态缓冲 ✓）改为**内核侧**的读回（若有 ✓）或
   至少在**建页之后**再取一次（`page` 缓冲 ✓）⇒ 确认页里 pc/left 是否已按 `arm_for` 写入 ✓；
② 若页内正确而 `uid` 仍 2000 ✗ ⇒ 只能建"别名读"（L 级 ⇒ 先计划 ✓）⇒ 判"内核是否写了 target" ✓；
③ 判据：`main: victim uid=0` ✓。
```

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
