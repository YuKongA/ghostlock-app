# 43499 的「只传参」改造：只差 10 个数字（2026-10-15）

## 字段现状（`backend/cve_2026_43499/access_params.hpp`）

```text
route（RouteId）, offset, offset_set
stamp_waiter_offset / stamp_task_offset / stamp_lock_offset / stamp_buffer_size
max_attempts / arm_hold / arm_sequence
park_ms / settle_us / consumer_delay_us / burst_calls / max_calls / fallback_lock_us
nice_modulus / policy
fake_task / fake_lock / target / value
**const profile::Document *document = nullptr;   ← 唯一的非标量**
另有 assembly_complete() 与 usable_for_initialize() 两个判定
```

⇒ 也就是说：**除 `document` 之外，全部是 u64/u32/bool 标量** ✓ —— 与用户裁定的「JNI 接口只能传参」天然吻合。

## `document` 到底被用来读什么（据 `layout_from_document.hpp`）

它提供三组共 **10 个数字**：

```text
五个 payload 偏移   （backend.cve_2026_43499.abi.payload）
两个内核物理数      （…abi.kernel）
三个镜像相对偏移    （…abi.offset）
```

（每条 route 自己那几个键 —— 计数 / 有符号位移 / 开关 —— 都已经是对应字段的候选值，由调用方填 ✓）

## 因此改造很小、很明确

1. 契约加 **10 个 LONG 字段**（例如 `payloadOffset0..4` / `kernelPhys0..1` / `imageOffset0..2`，命名待定，全局唯一即可）；
2. `Access43499Parameters` 去掉 `document`，改成这 10 个标量；`layout_from_document(…)` 改名并改成从参数读（它本来就只做「文档 → 数字」的搬运，且 `ImageArithmetic` 那四个数**本来就是调用方给的** ✓）；
3. Kotlin 从 HOCON 读这 10 个数（`DeviceProfile.long(path)` 现成 ✓）填进 DTO ✓ —— 于是接口上不再有 document，完全符合裁定；
4. 之后 43499 的读写原语（initialize / write / read / shutdown）才能接线（它们现在卡在参数类型上）。

## 顺序建议

先做第 1–2 步（契约 + 库内改参数），再做第 3–4 步（Kotlin 读值 + 原语接线），
每步都跑三门；这样 43499 侧与 43284 侧就都只剩「一条真正的缺口」（43284：SA 帧；43499：无 —— 它不需要 ESP）。