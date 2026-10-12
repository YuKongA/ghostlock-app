# 附件：现存日志点位清单（file:line，实测 18 处）

> 分析类（≤8 KiB）。**从主件 [43499-logging-points-design.md](43499-logging-points-design.md) §6 迁出**（配额纪律：单份 ≤8 KB；主件原超）。
> 相关：[判据的过滤规则（可执行形式）](43499-logging-points-annex-judging.md) · 本文表为 §4/§7 判据的**点位来源**。

## 6. 现存点位清单（file:line，实测 18 处）

| # | 位置 | 级别 | 键 | 数值 | 段 |
| --- | --- | --- | --- | --- | --- |
| 1 | multicast_route.hpp:371 | debug | requeue -> | - | C |
| 2 | multicast_route.hpp:391 | debug | requeue branch: settle grace | - | C |
| 3 | multicast_route.hpp:437 | debug | requeue <- ret | ret(1) | C |
| 4 | sandbox.cpp:438 | debug | [lua] surface -> call index | index(1) | E |
| 5 | sandbox.cpp:453 | warn | surface refused | Text | E |
| 6 | main.cpp:468 | debug/warn | prepare parity try | try, ok(2) | B |
| 7 | main.cpp:473 | debug/warn | prepare parity base | Hex(base) | B |
| 8 | main.cpp:475 | debug/warn | prepare parity delta | Hex(delta) | B |
| 9 | main.cpp:962 | warn | step refused | Text | D |
| 10 | main.cpp:964 | warn | step refused target | Hex(target) | D |
| 11 | main.cpp:1225 | warn | access -> route fallback | - | C |
| 12 | main.cpp:1230 | info | access -> facade takes the writes | - | C |
| 13 | main.cpp:1457 | debug/warn | victim -> uid | uid(1) | B |
| 14 | main.cpp:1663 | warn | retry -> uid not zero | uid, round(2) | D |
| 15 | main.cpp:2032 | info | launcher -> logging up/refused | - | A |
| 16 | main.cpp:2107 | info/warn | lua -> verdict clean/refused | - | E |
| 17 | main.cpp:2110 | info/warn | lua -> rejection code | code(1) | E |
| 18 | main.cpp:2116 | info | exit-code（**记进程真实退出码**，如 `rc=9` ⇒ `value=9`；**须为日志末行** = drain 判据；旧「Lua 判定 0/10」口径**已取代**） | code(1) | D |

**PI 内 5 处不在此表**（**环撤除后**由 `shared_` 字段承载 + **PI 之后打印**为 **6 类行**）：waiter-lock /
park-about word + park-about target-word address / park-return / ghost-disarm / attempt-door /
judge-outcome。**原 `window-trace-counters` 一行消失**：它是**环的元数据**（dropped 计数）；实测 **a=5 b=0**、**256 槽远未溢出** ⇒ **无信息损失** ✓。

**判据**：上表 18 条各自 grep -c 大于等于 1；窗口内 7 类各大于等于 1；删任一点则红。
