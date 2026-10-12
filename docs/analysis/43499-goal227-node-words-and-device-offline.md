# 43499 · 第 227 轮：落第一处缺口（`right`/`left` 各三字）✓ ；**真机验证被"设备离线"阻塞** ✗

## 一、已落的改动（✓，门禁绿 ✓）

```cpp
// payload_page.cpp（第 227 轮 ✓，权威 `origin/main:util.cpp:459-465` 逐字 ✓）：
const std::uint64_t node_owner = layout.parent;                       // = target − pointer ✓
put_pointer(page, waiter_at + inputs.waiter.**right** + 0x00U, node_owner, pointer) ✓
put_pointer(page, waiter_at + inputs.waiter.**right** + 0x08U, 0U, pointer) ✓
put_pointer(page, waiter_at + inputs.waiter.**right** + 0x10U, 0U, pointer) ✓
put_pointer(page, waiter_at + inputs.waiter.**left**  + 0x00U, node_owner, pointer) ✓  … +0x08 / +0x10 同理 ✓
⇒ 依据：权威「`RIGHT_OFF`/`LEFT_OFF` **各是三字节点**：`+0x00 = fake_parent` ✓、`+0x08 = 0` ✓、`+0x10 = 0` ✓」
⇒ 而本树此前**只写 waiter 的三单字** ✗ ⇒ 节点内另两字是垃圾 ⇒ 内核遍历时读到它 ✗。
```

## 二、门禁（✓，全绿 ✓）

```
`android` → RC=0，**0 error / 0 warning** ✓ ；`check` → **109 PASS** ✓
```

## 三、被阻塞的验证（✗，非代码问题 ✓）

```
`adb devices -l` ⇒ **列表为空** ✗ ；`adb shell` ⇒ `no devices/emulators found` ✗ ；`adb reconnect offline` ⇒ 无效 ✗
⇒ ⇒ **⇒ 设备（`QV770MFGJ1` ✓）已从主机断开** ✗ ⇒ 我**无法**在代码侧恢复物理连接 ✓
⇒ ⇒ ⇒ **⇒ 待设备重新连接后，只需重跑**：`adb push build/native/ghostlock /data/local/tmp/ghostlock` +
   `adb shell "cd /data/local/tmp && ./ghostlock < wire-43499-collide-F.framed"` ✓ ⇒ 判据 `main: victim uid=0` ✓
```

## 四、下一处待落（✓，已定位 ✓）

```
第二处缺口：**`fill_profile_cred_copy`**（⇒ `needs_credential_copy` 触发 ✓，权威 `util.cpp:466` ✓）——
   本树**只置标志、无调用** ✗（第 223 轮已证 ✓）⇒ 位置：`payload_page.cpp` 的 `if (layout.needs_credential_copy)` ✓；
   实现需参照 `origin/main:src/core/support/util.cpp:191` 的 `fill_profile_cred_copy`（⇒ 约 30 行 ✓，未读 ✓）。
```
