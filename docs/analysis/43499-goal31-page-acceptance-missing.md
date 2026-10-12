# 43499 · 第 31 轮（「瓶颈查旧代码」）：~~页的采纳判据在生产路径缺失~~ ⇒ **第 35 轮更正：早已存在** ✗✓

> **更正（第 35 轮 ✓，必须读）**：本文件第 31 轮的结论 **"生产路径零调用"是错的** ✗。
> 真相：`main.cpp` 里**早就有**这道门 ✓ ——
> `:880 main: gate probe route=%s accepts=%d` ✓（门探针）与 `:886-889`：
> `if (!layout_accepts_page(page_request, …)) { victim.retire_child(); return "payload-layout-not-accepted"; }` ✓
> （而 `layout_accepts_page` 内部**先**调 `layout_matches_request` ✓，见 `write_layout.cpp:35` ✓ ⇒ **臂与偶字节两条都在门内** ✓）。
> 我第 31 轮的 grep 之所以只看到测试 ✗：输出被 `head -10` 截断 ✗（⇒ 又一次"grep 方法与截断"的教训 ✓）。
> 因此：第 35 轮新增的重复门**已撤回** ✓（不可达 ✗），树回到第 34 轮状态 ✓，门禁全绿 ✓。
> 遗留价值：第 34 轮那次 `accepts=0` 的读数 ✓ ⇒ 说明**这道门在真机上确实会拦页** ✓（门不是摆设 ✓）。

## 一、旧码（✓，`spray.cpp:682-714` 逐字 ✓）

```cpp
uintptr_t prepare_good_kernel_page(const WriteRequest &request) {
    for (attempt = 1..max_attempts) {
        base = prepare_kernel_page(&request);
        if (base) {
            layout = {parent, right, left, fops};                       // 本次准备得到的元组 ✓
            if (!payload_write_layout_matches_request(&request, &layout))
                pr_warning("payload arm mismatch preserve_child=%d right=%016zx\n", …);   // ✗ 臂不符
            else if (!payload_write_layout_accepts_page(&request, &layout))
                pr_warning("page %016zx stores an even byte over selinux_state.initialized;
                           taking another\n", base);                                       // ✗ 偶字节
            else return base;                                          // ★ **只有两条都过才采用** ✓
        }
        … 超时/重试（heap_prepare_max_attempts / heap_prepare_timeout_ms ✓）…
    }
}
```

## 二、本树（✓，第 31 轮 grep ✓）

```
判据**本体在** ✓：`write_layout.hpp:55 layout_matches_request` ✓ / `:61 layout_accepts_page` ✓
                （`write_layout.cpp:27/34` 实现 ✓，注释含"second byte … state field" ✓）；
但**调用点全在测试里** ✗：`core/src/tests/write_layout_test.cpp`（:16/:17/:84…:113 ✓）⇒
⇒ ⇒ **生产路径零调用** ✗ ⇒ 从不因"臂不符/偶字节"而**换一页** ✗ ✗。
```

## 三、为什么这能解释"stamp 落地(99 ✓) 而写不生效 ✗"（✓✓）

```
页只要被"采纳"，其字节就会被送进内核 ⇒ **stamp 一定落地** ✓（99 ✓ 与观测一致 ✓）；
而页里若臂不符（preserve 与请求相反 ✗）或落在**偶字节**（会清掉 selinux_state 附近的状态位 ✗），
**擦除式写入就不会作用到目标** ✗ ⇒ ⇒ 于是"落地但不生效" ✓✓ —— 与几十轮的观测**逐条吻合** ✓。
```

## 四、修法（✓，下一步；属攻击路径 ⇒ 先计划后码 ✓ 但本页即计划 ✓）

```
1. 在建页处（`main.cpp:814` 附近的 `build_payload_page` ✓ 之后、发送之前 ✓）加入两条判据 ✓：
   `layout_matches_request(request, layout)` ✓ 与 `layout_accepts_page(request, layout)` ✓；
2. 任一不过 ⇒ **不发送**，走"换一页"⇒ 用既有重试（`steps.*_attempts` ✓）再来一轮 ✓；
3. 轮数耗尽 ⇒ **具名拒绝** ✓（如 `page-not-accepted` ✓，不假装成功 ✓）；
4. 打印与退役同形的两行 ✓（`payload arm mismatch …` / `stores an even byte … taking another` ✓），便于真机判读 ✓。
判据：`main: victim uid=0` ✓；辅助：上述两行**零出现** ⇔ 页被采纳 ✓。
```

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```
