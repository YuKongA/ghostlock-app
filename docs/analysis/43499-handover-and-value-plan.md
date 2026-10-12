# 43499 · 意图确认 + 计划 + 交接清单（round 213）✓

## 意图确认（按退役源码 ✓，比日志更硬 ✓）

```
快照 :282  `payload_write_layout(const WriteRequest *request, std::uintptr_t page_base, …)` ✓
⇒ 退役的"要写的值"是**由请求 + 页基址算出来的** ✓（不是文档里的常数 ✓）；
而 75 份归档设备日志**都不记录**该值 ✗ ⇒ 以源码为据 ✓ ⇒ ⇒ **意图明确**：
    应喂 `write_layout.value`（= 由 page_request/geometry 算出 ✓）
⇒ 而本树 `main.cpp:1046` 喂的是文档的 `payload.value`（= 0x4141414141414141 ✗）⇒ **接错** ✓。
```

## 清单（下一段按此推进 ✓）

| # | 事项 | 依据 | 性质 |
| --- | --- | --- | --- |
| 1 | `payload.value` 用了毒值 `0x4141414141414141` ✗；真值 `write_layout.value` 未喂 | 退役 `payload_write_layout(request, page_base, …)`（快照 :282 ✓）= **值由请求/几何算出** ✓ | **L 级**（攻击路径）⇒ 先计划 + 真机门禁 ✓ |
| 2 | `main` 开头关 stdout 缓冲（`setvbuf` ✓） | 本轮实证：早退时日志**丢尾巴** ✗（1190 字节截断 ✓） | S 级 ✓ |
| 3 | select 分支的 `select_config.value` 同样来自毒值 ✗ | `main.cpp:968` ✓ | 与 1 同批 ✓ |
| 4 | `PiRace` 已绑定**真 seam** ✓（此前空实现 ✗） | `pi_race.hpp:62-86` ✓ | 已修 ✓ |
| 5 | 引擎**未武装**已修 ✓（`race_.prepare(wait,target)` ✓） | `run` 的第一行 gate ✓ | 已修 ✓ |

## 本晚 43499 的净成果（可核 ✓）

```
· 两个根因级修复：空实现 seam ✗ ⇒ 真 seam ✓；引擎未武装 ✗ ⇒ 已武装 ✓；
· 一条行号级的接错定位：真值算出未喂 ✔（本轮 ✓）；
· 第二条 route（select_stack）从零写到"与快照逐条同形" ✓（词号/常量/主线程 pselect/三演员/对象/构建时机 ✓）；
· 三次自纠均留档 ✓；门禁全程绿 ✓（check 108 PASS ✓ / android 0 警 ✓ / lint 0 ✓）。
· `uid=0` **仍未取得** ✗ ⇒ 从未声称 ✓。
```
