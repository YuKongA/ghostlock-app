# 计划（L 级）：把写值从"文档里的毒值"改回"由请求+几何算出的真值"

> 依 AGENTS §2：攻击关键路径的改动属 **L 级** ⇒ **先出计划、获认可后再写码** ✓。本页即计划 ✓。

## 一、动机（有行号 ✓）

```
main.cpp:488   route_values.value ← 文档 `backend.cve_2026_43499.payload.value`
                 实测该键 = **4702111234474983745 = 0x4141414141414141** ✗（"AAAAAAAA" 毒值 ✓）；
main.cpp:777   page_write_value = write_layout.value ✓（**真值**，由 page_request + 几何算出 ✓）；
main.cpp:1046  stage_context.payload_value = route_values.value ✗（喂给阶段的仍是毒值 ✓）。
⇒ ⇒ 真值算出来了却没有喂给路由/阶段 ⇒ 写下去的是 "AAAA" ⇒ 内核侧状态不变 ✓。
退役旁证：快照 :282 `payload_write_layout(const WriteRequest *request, std::uintptr_t page_base, …)`
         ⇒ 退役的写值**由请求+页基址算出** ✓（不是文档常数 ✓）。
```

## 二、影响文件（预计 ✓）

| 文件 | 改动 |
| --- | --- |
| `core/src/main.cpp` | `:1046` 改用 `page_write_value` ✓；select 分支 `select_config.value`（`:968`）同源改 ✓ |
| `core/src/backend/cve_2026_43499/seam/select_stack_route.hpp` | 无需改（它只消费配置 ✓） |
| `core/fixtures/wire-43499-collide-F.json` / `-select-stack.json` | 评估是否把 `payload.value` 改为**真值来源**或删除该键（若删 ⇒ 需同步 expected_keys ✓） |

## 三、行为差异（可判 ✓）

```
改前：写入值 = 0x4141414141414141 ✗ ⇒ 内核 S 位上落的是无意义字节 ⇒ `enforce` 恒 1 ✗；
改后：写入值 = write_layout.value ✓（= 退役的算法 ✓）⇒ 若原语成立 ⇒ `enforce` 1→0 ✓；
⇒ ⇒ 这正是本晚唯一从未观测到的读数（`main: selinux enforce=0` ✗）⇒ 判据清晰 ✓。
```

## 四、验证计划（✓，按 AGENTS §8）

```
① host：make -C core check（108 PASS ✓ 基线）+ android 零告警 ✓ + markdownlint ✓；
② 真机：同夹具多次统计（两支都跑 ✓）⇒ 看 `main: selinux enforce=` 是否出现 0 ✓；
③ 出现 0 ⇒ 再看 `main: victim uid=0` ✓（目标级）；④ 归档到 docs/analysis/device-gates/ ✓。
```

## 五、风险与回退（✓）

```
· 若"写值本身"是退役的**故意设计**（例如以毒值探测可写性 ✗）⇒ 改后行为会变 ✗ ⇒ 需以 §四② 的读数为准 ✓；
· 回退：两处赋值改回 `route_values.value` 即复原 ✓（改动极小 ✓）；
· 本改动**不动** wire/profile 契约本身 ✗（只改"喂哪个值" ✓），故不需 ADR ✓。
```
