# 帧就是 IpSecManager 的会话秘密 —— 长期阻塞项的解（2026-10-15）

## 原文（session_frame.hpp 头注释）

> `--ghostlock-app-call` with `--enable-status-record` reads one length-prefixed GLKv3 document …
> When — and only when — the decoded selection is the cve_2026_43284 backend, the same stdin may carry
> one additional length-prefixed frame holding the **IpSecManager session secrets**.
> The profile layer never understands it; 43499 never reads it …

⇒ **帧里的材料来自 App 自己的 `IpSecManager` 会话**（不是外部上位机生成的）。

## 帧格式（冻结，84 字节 payload）

```text
u32 be payload_len = 84（不在 84 里）
+0  u8  version = 1
+1  u8  kind = 1 (IPSEC_SA)
+2  u16 be reserved = 0
+4  u32 be spi
+8  u16 be encap_port
+10 u16 be sender_port
+12 u8  icv_len = 16
+13 u8  reserved2[3] = 0
+16 u8  aes_key[32]
+48 u8  hmac_key[32]
+80 u32 be trailer = 0x34383238
```

其它约定：整数一律大端；不匹配（version/kind/长度/reserved/icv_len/trailer、短读、缺帧）一律 **fail-closed**；
秘密是**会话秘密**：绝不进 profile::Document、不进序列化 profile、不进 argv、不进日志；容器内零化。

## 这解开了什么（修订 §11.30 与 §11.33）

| 之前的结论 | 修订后 |
|---|---|
| §11.30 公开 API 不让指定 SPI ⇒ 无法让内核装上「我们要的那条 SA」 | **不需要指定**：用公开 API 建一条会话，**读回**系统分配到的 SPI 与端口，连同我们自己提供的两把密钥写成帧 ⇒ 材料与内核那条 SA 天然一致 |
| §11.33 内核里必须有匹配的 SA ⇒ 需要系统权限 | **不需要**：那条 SA 正是**我们自己**通过 IpSecManager 建的（App 权限足够） |

于是整条写路径闭合：

```text
Kotlin：IpSecManager 建传输模式会话（公开 API）→ 导出 SPI/端口/两把密钥/icv_len → 写 84 字节帧
native/工具：read_session_secret_frame → SaMaterial → RealSpliceIo.splice_one（ESP 报文 + splice 目标页）→ 目标页被改写
```

## 下一步

1. Kotlin 侧：建 IpSec 会话并把材料导出成帧（公开 API 的 `IpSecTransform`/`IpSecManager`；导出哪些能读到、哪些要我们自己提供，读代码确认）；
2. native 侧：薄原语接收这份材料（或按现有 `kFramePath` 约定写帧文件，由 native 读）；
3. 然后 patch#1 真机复跑 —— 判据不变：crash_dump64 的 offset 16 窗口与 `embeddedImage(0)` 对应字节一致。