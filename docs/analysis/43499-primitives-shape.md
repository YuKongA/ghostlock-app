# 43499 读写原语的形状（2026-10-15 读 kernel_access.hpp）

## 差异要点（与 43284 比）

```text
通道：AccessChannel::PiWindow   ← 不是 PageCache（supports(PiWindow)=true，其它通道一律 Unsupported）
方法：initialize(const AccessMiscParameters&) → AccessResult<bool>
      write(address, span<const std::byte>, AccessChannel, const AccessMiscParameters&) → Result
      read(…同上形状…) → Result
      shutdown() → AccessResult<bool>
```

## 因此四个薄原语这样写（原生侧只做「查表 + 调既有方法 + 回来」）

```cpp
int  device_initialize_43499(accessId, route, offset, offset_set, const contract::Access43499AbiValues &abi) noexcept;
     // 组 Access43499Parameters{route, offset, offset_set} + 那 10 个 ABI 数字 → initialize → 0 或状态码
bool device_write_43499(accessId, address, const std::vector<std::uint8_t> &block, const char **why) noexcept;
     // channel = AccessChannel::PiWindow；失败带具名（含 status_name）
std::vector<std::uint8_t> device_read_43499(accessId, address, std::size_t bytes, const char **why) noexcept;
     // 同上通道；读回字节由 Kotlin 决定怎么用
int  device_shutdown_43499(accessId) noexcept;   // 0 或状态码
```

Kotlin 侧（`Assembled43499`）对应补 `initializeAccess(...)` / `writeBlock(...)` / `readBlock(...)` / `shutdown()`，
顺序与参数全在 Kotlin ✓（与 43284 侧同一套形态）。

## 与 43284 的关键差别（对目标选择的影响）

- 43284 走 ESP（要 SA/帧，且写目标必须是可 insmod 的路径）；
- **43499 走 PiWindow**，**不需要 ESP、不需要帧** ⇒ 它的读写链路**不依赖任何外部材料** ✓；
- 所以 43499 侧可以直接把读写跑到真机上，看它自己能做什么（这也是 J7.5-6 的前提）。
