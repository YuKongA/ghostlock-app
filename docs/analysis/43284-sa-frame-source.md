# SA 材料的来源：会话秘密帧（2026-10-15 读 phase_credentials）

```cpp
bool phase_credentials(SaMaterial &sa) {
    if (!g_frame_from_stdin) {
        fd = ::open(kFramePath, O_RDONLY | O_CLOEXEC);          // 一份「会话秘密帧」文件
        if (fd < 0) { printf("phase 1 refused, no frame at %s"); return false; }
    } else { /* 临时启动器模式：--frame-stdin，帧从 stdin 来 */ }
    const SessionFrameStatus read = read_session_secret_frame(fd, &sa);   // 解析出 SPI / 密钥 / icv_len
    if (read != Ok) return false;
    sa.set = true;   // 注释：Declaring the material usable is the caller's act, as the frame header says
    printf("phase 1 ok, credentials held (spi=0x%08x icv_len=%u)", sa.spi, sa.icv_len);
}
```

⇒ 三条事实：

1. **SA 材料（SPI / 加密与认证密钥 / ICV 长度）来自一份文件**（`kFramePath`，或经 `--frame-stdin` 从 stdin），
   由 `read_session_secret_frame` 解析；没有帧就**具名拒绝**（不猜、不默认）；
2. 帧头自己声明：**「声明材料可用是调用方的事」** —— 帧只是秘密材料，能不能用由调用方负责；
3. 材料是**运行时秘密**（SPI/密钥/端口），按项目规则**绝不进文档、走帧、用后清零** —— 与既有约定一致。

## 还差的一环（下一步）

**谁产出这份帧**。两条可能：

- A：外层（操作者/上位机）按设备生成并放进 `kFramePath`；
- B：**App 自己用公开的 IpSec API 建一条 SA，再把系统分配到的 SPI 与它自己提供的密钥写成帧** ——
  这条能把 §11.30「公开 API 不让指定 SPI」与 §11.33「内核里必须有匹配的 SA」**同时解开**：
  我们不需要指定 SPI，我们只需要**读回**系统给的那条 SA 的材料。

下一步：grep `kFramePath` 与 `read_session_secret_frame` 的**产出侧**（谁写帧、格式如何），
以及找找有没有「从 IpSec 会话导出材料」的既有代码；这决定 A 还是 B。