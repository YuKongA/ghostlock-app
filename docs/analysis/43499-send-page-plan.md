# 43499 · 计划：把页面真正送进内核（L 级，攻击路径）

## 为什么必须做这一步（根因，已证）

- 本树 `LinuxHeapSeam::send_buffer` 发的是**静态全零缓冲** ⇒ 内核里的 skb 数据是零 ✗；
- 我证对过的页面字节**从未被发送** ⇒ 内核看不见页面 ⇒ 写永不落地 ✓；
- 本树自己承认：`prepare_page.hpp` 头部 "NOT YET IN THE SEQUENCE … **That write is not ported yet**" ✓；
- 退役的机械（`spray.cpp`）：`base = leaked & ~(ORDER3_SIZE-1)` → `prepare_skb_payload(base, …)`（写页面）
  → `sendmsg(pcp_shaping_sv[0], …)`（发出）✓。

## 选项 A：准备阶段内部完成（页面由调用方给）

- 形状：`PrepareRequest` 增加"页面镜像"输入（span + 长度）；准备阶段在**对齐之后**把该镜像写进它要发送的缓冲
  （按 `ORDER3_SIZE` 分块 + `chunk_bias` ✓），再用 `send_bytes` 发出 ✓；
- 问题：页面里的**所有指针**要用 `base` 算 ✓，而 `base` 是准备阶段**刚算出来**的 ⇒ 调用方**无法在调用前**填好指针 ✗
  ⇒ 除非把"指针回填"也交给准备阶段（即它得懂页面布局 ✗ 分层被破坏 ✗）。

## 选项 B：准备之后补一次发送（推荐 ✓）

- 形状：准备阶段把 shaped block 的**一端 fd 交出来**（`PrepareReport.fds` 或既有 `offer_fds` 通道 ✓），
  并交出 `base` ✓（已有 ✓）；main 拿到 `base` 后：
  1. `payload_base = base + payload_delta`（delta 走 profile/wire ✓，不写死 ✓）；
  2. 在**本地缓冲**里按退役布局构建页面（指针全用 `payload_base` ✓）——这与我现有建页代码同形 ✓，
     只是页面**不再放 mmap** ✗，而是放"即将被发送的缓冲" ✓；
  3. `send_bytes(fd, buffer, len)` 发出 ✓ ⇒ 内核 skb 里就是这份页面 ✓；
  4. 之后照旧跑窗口三步 ✓。
- 优点：分层不动 ✓（准备只管对齐与持有 ✓；页面构建仍在 main/页面单元 ✓）；与退役的**两步顺序**逐字一致 ✓。
- 代价：`PrepareReport` 要暴露 fd（新字段 ✓，非默认：未交出即具名拒绝 ✓）。

## 判据与验证

- **主判据**：受害者自报 `uid=0`（`main: victim uid=0`）✓；
- **旁证**：写落地探针（comm len 变 0/8 ✓）、`main: route status=0/0 … successes≥1` ✓、无任何具名拒绝 ✓；
- 真机门禁：冷机、单 route、KernelSU 未加载 ✓；归档到 `docs/analysis/device-gates/` ✓。

## 与我现有代码的关系（最小改动面）

- 现有"mmap 建页 + alias_delta"✗ 在选项 B 下**整段替换**：页面改为构建在发送缓冲里 ✓，
  `alias_delta` 不再需要（`payload_base` 来自泄漏 ✓）⇒ 那处"别名实验"注释同批更新 ✓；
- `payload_for_zero_write()`（奇偶挑页 ✓）在选项 B 下也**不再需要** ✓（判据吃的是 `payload_base` ✓）
  ⇒ 同批删除并更新其注释与测试 ✓；
- 两相位 ✓、`owner_started` ✓、`nr_requeue/val3` ✓、三个锚 ✓、凭据接线 ✓ 全部保留 ✓。
