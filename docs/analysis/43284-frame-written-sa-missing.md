# 帧已落盘；patch#1 仍卡在「写」—— 缺的是那条 SA（2026-10-15 真机）

## 真机逐行（A301SO，APK 1010）

```text
typed: sa frame written=88 spi=0x25e1a0c3 sha256=ad11bb72f929e7cc      ← ★ 帧落盘，88 = 4 + 84（格式自证）
typed: patch1 door=17179869185 refusal=                               ← 门开了
typed: patch1 init=true refusal=                                      ← initialize 成功
typed: patch1 wrote=false refusal=image-write:step-write-refused:call-failed   ← 仍在第 ④ 层（land_block 失败）
```

## 解读（与设计完全一致）

1. **帧只是材料**，落盘不等于把 SA 装进内核 ✗ —— 我们申请了 SPI（`0x25e1a0c3`，每次不同 ✓）、开了 encap 4500 ✓，
   但**还没有用这些材料建立那条传输模式 SA**（`IpSecManager` 的转换那一步 ✗）；
2. 写路径经 ESP 报文 ⇒ 内核里必须有**与报文同 SPI 的 SA** ✓；没有 ⇒ 报文被丢弃 ⇒ `land_block` 失败 ✓
   ⇒ 现象与 §`write-path-is-esp` 的机制**完全对上** ✓；
3. 所以下一步不是改代码里的参数，而是**把 SA 建起来**（用我们申请到的 SPI + 4500 + 两把密钥）。

## 下一步（在 app 自己 uid 内）

1. 探针里问清 `IpSecManager` 建传输模式 SA 的**可用形态**（公开面：`IpSecTransform.Builder` + socket 绑定；
   注意 §11.30 的实测：`IpSecConfig` 是系统 API ✗、SPI 不能指定 ✗ —— 所以要问的是「我们申请到的 SPI 能否被用上」，
   若不能，则下一条：看上游怎么把 allocateSPI 与建 SA 串起来（`upstream-dirtyfrag-handling.md:227` 的顺序））；
2. 建好之后**保持 encap 套接字打开**（关掉会拆封装 ✗）—— 探针里目前是开完就关 ✗，要改；
3. 重跑 patch#1：判据仍是 `crash_dump64` 的 offset 16 窗口与 `embeddedImage(0)` 一致。
