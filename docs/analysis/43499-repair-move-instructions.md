# 43499 · 修复轮并入窗口路径：精确搬移指令（round 139，待执行）

## 为什么（三处证据 ✓）

1. `WindowGuard` 可重用 ✓（`enter()`/`reclaim()`，且数 `reclaim_count_` ✓）⇒ 开第二轮**合法** ✓；
2. `WindowAttempt{chain,target,window,route,payload_target,payload_value}` ✓ + `window_attempt_once<Seam>` ✓
   ＝"进出窗口的一次尝试"（三演员 ✓）⇒ 修复轮本该这样跑 ✓；
3. 真机：直接 `execute` ⇒ `status=2/61`（消费者没赢 ✗）；主轮同样引擎却 `0/0` ✓ ⇒ 差别就是**窗口** ✓；
4. 退役顺序：主轮 → W1b/W2b（**在之后** ✓）。

## 搬移指令（下一轮照单执行 ✓）

```
① 剪切：core/src/main.cpp  第 806–884 行
   （起：注释「★ 修复轮（退役 A 路…」；止：`if (repair_target != 0) { … }` 的收尾花括号，
     其后一行是 `route_values.fake_task = page_inputs.fake_task;`）
② 粘贴：插到"写后读一次 comm 作为旁证"那行注释**之前**（主轮报告打印之后 ✓）
   ⇒ 该处 `words_region` / `window_guard` / `named_step` / `report.step_stage_*` 全部在作用域内 ✓
③ 改内容：把块内那行
   `(void) repair_route.execute(repair_values.target, repair_values.value, 0U);`
   换成照抄主轮的四件套（同一守卫 + 同一套尝试一次）：

   cve49::steps::WindowStageContext repair_stage{};
   repair_stage.chain   = words_region.word(0);
   repair_stage.target  = words_region.spare(0);
   repair_stage.window  = &window_guard;
   repair_stage.route   = &repair_route;
   repair_stage.payload_target = repair_values.target;
   repair_stage.payload_value  = repair_values.value;
   cve49::steps::WindowStageRequest repair_stage_request{};
   repair_stage_request.attempts     = (uint32) report.step_stage_attempts;
   repair_stage_request.settle_us    = (uint32) report.step_stage_settle_us;
   repair_stage_request.chain_rounds = (uint32) report.step_stage_extra;
   (void) cve49::steps::run_window_stage_with_attempt(named_step, repair_stage_request, repair_stage);

④ 同时删掉块内那段 TODO 注释（其目的即本搬移 ✓）。
```

## 判据

- **两轮修复均 `status=0/0`** ✓（与主轮同形 ✓）；
- 随后回到 `uid=0` ✓。

## 现在的树

- android RC=0（0 警）✓、check 108 PASS ✓、lint 0 ✓（round 138 末已恢复全绿 ✓）。
