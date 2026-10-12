# 43499 · 接错一处：真值算出来了却没喂给 route（round 212 收束）✓✓✓

## 三条行号证据（逐字 ✓）

```
main.cpp:488   num("backend.cve_2026_43499.payload.value", route_values.value);
                 ⇒ 文档里的值是 **4702111234474983745 = 0x4141414141414141** ✗（"AAAAAAAA" 毒值 ✓）
main.cpp:777   page_write_value = write_layout.value;
                 ⇒ **真值**（由 page_request/geometry 算出 ✓）—— 但只赋给了局部变量 ✓
main.cpp:1046  stage_context.payload_value = route_values.value;
                 ⇒ **喂给阶段的仍是文档的毒值** ✗ ⇒ ⇒ 写下去的是 "AAAA" ⇒ 内核侧状态不变 ✓
```

## 为什么 canonical 也一样（✓ 自洽 ✓）

```
canonical 夹具的 `value` 同样是 **4702111234474983745** ✗ ⇒ 两支都写 "AAAA" ✓；
⇒ 这与本晚 canonical "≥9 次完整跑、enforce 从不 0" ✗ **完全一致** ✓ ⇒ 不是巧合 ✓。
```

## 下一步（下一批 ✓，先核对再改 ✓）

1. **先核对意图**：从归档日志反推退役当时"要写的值"是什么 ✓（若确为页派生值 ⇒ 修法明确 ✓；
   若文档本应给出真值 ⇒ 是**夹具**的问题 ✗ ⇒ 改夹具 ✓ —— 两条路不能猜 ✓）；
2. 若确为"应喂 `write_layout.value`" ⇒ 改 `:1046`（以及 select 分支的 `select_config.value` ✓）⇒ 属**攻击路径改动** ⇒
   **先出计划 + 真机门禁** ✓（按 AGENTS §2 ✓）；
3. 同时：`main` 开头关 stdout 缓冲 ✓（本轮发现日志会丢尾巴 ✗）。

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
