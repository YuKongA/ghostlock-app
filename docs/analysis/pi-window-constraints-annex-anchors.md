# 附件：PI 边界锚点引证（原文，只引锚点行）· pi-window-constraints

> 分析类（≤8 KiB）。**从 [pi-window-constraints.md](pi-window-constraints.md) 边界段迁出**（配额：主件并入锚点后超 8192 B）。
> **权威 = 代码里的 `PI-*` 注释标记**；本件只逐行引其原文。**数字截至 2026-10-12，会随改动漂移**（漂移只改这里的数字）。

- **waiter_main / owner_main**：`core/src/backend/cve_2026_43499/landing/multicast_route.hpp`
  - `:641` `/* PI-IN ：park 前最后一次同步 */`
  - `:699` `/* PI-OUT：摘除完成 => 本线程的 PI 段到此为止 */`
  - `:705` `/* PI-IN ：owner 从这里进入 PI（取目标锁） */`
  - `:732` `/* PI-OUT：owner 交还目标锁 */`
- **trigger**（本轮新增锚点）
  - `:552-553` `/* PI-IN-TRIGGER：trigger 全体在 PI 内（唯一调用点是 waiter_main 的 PI 段之内、park 之后 —— 拷贝必须落在"park 刚返回"的那个线程栈上）=> 本函数没有 PI 之外的尾部。 */`
  - `:603-604` `/* PI-OUT-TRIGGER：trigger 自己的跨度到此为止。注意：PI 没有结束 —— 本函数返回后仍回到 waiter_main 的 PI 段内（那边的 PI-OUT 在摘除之后）。 */`
- **consumer_main**
  - `:741-743` `PI-IN-CONSUMER`（进入 PI 取目标锁）
  - `:793` `PI-OUT-CONSUMER`（交还目标锁）
- **judge（豁免）**
  - `:512-514` `PI-EXEMPT-JUDGE`（judge 整函数在 PI 之外 ⇒ 其豁免出处）
