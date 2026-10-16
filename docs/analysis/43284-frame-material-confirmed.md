# 43284 的会话材料：两部分都经真机确认（2026-10-15）

## 真机（A301SO，APK 1010）

```text
typed: ipsec spi allocated=0x4f17092b      ← 每次都能申请到（上一次是 0x7afb293）
typed: ipsec spi closed
typed: ipsec encap opened port=4500        ← ★ encap 端口按我们指定的 4500 开成功
typed: ipsec encap closed
```

（`./gradlew deviceGate` → PASS，38 项不变 ✓）

## 结论：84 字节帧的六个字段全部有来源，且都可自产

| 字段 | 来源 | 状态 |
|---|---|---|
| spi | allocateSecurityParameterIndex | ✅ 真机两次都拿到 |
| encap_port | openUdpEncapsulationSocket(4500) | ✅ 真机开成功 |
| sender_port | 我们自定（或 0 由内核挑） | ✅ 可控 |
| icv_len | 冻结值 16 | ✅ |
| aes_key / hmac_key | 我们自定 | ✅ |

## 下一步（很具体）

1. Kotlin 侧写一个**帧装配**：按冻结格式拼 84 字节（payload_len 前缀 84、version=1、kind=1、reserved、spi、encap_port、
   sender_port、icv_len=16、reserved2[3]、aes_key[32]、hmac_key[32]、trailer=0x34383238，整数大端）；
   两把密钥用 Kotlin 侧随机生成（或固定测试值 ✓ 先跑通）；
2. 落盘 `/data/local/tmp/ghostlock-sa.frame`（既有 `read_session_secret_frame` 就读这个路径 ✓）；
3. 重跑 patch#1 —— 判据：`crash_dump64` 的 offset 16 窗口与 `embeddedImage(0)` 一致；
4. 之后 carrier 写测试 → hook → trigger → wait（终态对齐 B5-9h）。

## 注意（帧格式的校验很严）

`session_frame.hpp` 写明任何不匹配（version/kind/长度/reserved/icv_len/trailer、短读、缺帧）都 **fail-closed** ✓；
所以拼帧时：**大端**、`payload_len` 是 4 字节前缀（不含在 84 里）、trailer 必须是 0x34383238 ✓。
