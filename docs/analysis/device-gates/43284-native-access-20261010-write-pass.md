# 43284 新 kernel access 写面：真机 PASS（2026-10-10）

设备 A301SO / `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`，冷机（无 root、KernelSU 未加载）、**锁屏**。

## 判据与结果

| 判据 | 结果 |
| --- | --- |
| 落盘块数 | `phase 3 ok, 769 blocks written and verified at 0x0`（12304 B 的 `ghostlock.ko` 全部） |
| 逐块回读 | 每块 write 后 read 比对通过（769/769） |
| 独立校验（载体页缓存） | `sha256(ghostlock-carrier)` 4fe7b59a… → ad423012…；头 16 B = `7f454c46020101000000000000000000`（模块 ELF 头） |
| **未达成** | 模块未被加载：`phase 4 timeout after 5000 ms`（patch #1 / hook / trigger 链未移植） |

## 现场（可复现）

1. 临时启动器（Kotlin，App 内 TempLaunchActivity；锁屏下 am start 即可）：IpsecManager 建 SA → 写出 88 B 会话帧 → 帧留在 App 外部目录供 adb 取用，SA 在 Activity 存活期间保持安装。
   - `GLK-TEMP: SA ok spi=0x7afd1028 encap=37966 sender=38410 icv=16`
2. `adb shell cp /sdcard/Android/data/com.ghostlock.app/files/ghostlock-sa.frame /data/local/tmp/ghostlock-sa.frame`
3. `adb shell /data/local/tmp/pagecache_load_lkm`（相 1 凭据 → 相 2 模块先验 → 相 3 写 + 回读 → 收）

## 本批由真机测出并修掉的两个真实缺陷

1. **CBC IV 丢了期望值那一半**（旧树 `compute_cbc_iv` 的移植丢参）：内核就地解密后 `new = D_K(old) xor IV`，要写出 desired 必须 `IV = D_K(old) xor desired`；原实现只做 `D_K(old)` ⇒ 每次写回**零**（载体本就全零 ⇒ 页缓存哈希纹丝不动，与现象吻合）。
2. **ICV 覆盖的对象错**：`splice_one` 把线包的密文槽替换成页里现有字节，而 ICV 是按期望块算的 ⇒ 内核（authenc 先验后解）必然丢包，页一字节不改。改为按 `old_block` 计算（旧树同行即此）。

两处修完，同一 SA 下立刻 769/769 通过；host 侧 esp_crypto_test / real_splice_io_test / pagecache_write_test / pagecache_chain_test 全绿。

## 尚未完成（下一批）

- 载体的 vendor 路径仍需 crash_dump64 桥（`/vendor/...` 对 shell 不可读，本批退回一次性载体）；
- patch #1（splicehelper → crash_dump64）、libc++ sentry hook 规划与 shellcode、trigger 未移植 ⇒ 写完之后无人加载模块；
- 临时启动器与工具的 `--frame-stdin` 已登记为临时物，随 SA 供给进生产路径同批删除。

---

## 第二轮（同日）：patch #1 + crash_dump64 桥 + **vendor 载体落盘**

| 判据 | 结果 |
| --- | --- |
| patch #1 | `phase 3a ok, helper image (1440 bytes, 90 blocks) written to /apex/com.android.runtime/bin/crash_dump64@0` |
| vendor 载体 | `phase 3 ok, 769 blocks written and verified at 0x0 of /vendor/lib64/libbinderdebug.so` |
| 独立核验（经桥直读 vendor 页） | `@16` = `0100b700010000000000000000000000`、`@4096` = `3f2303d5a88884d28824a5f2c887d4f2`，与 `ghostlock.ko` 同偏移逐字节一致 |
| **未达成** | 模块未被加载：`phase 4 timeout after 5000 ms`（hook 链未移植） |

### 本轮测出并修掉的问题

1. **ESP 序列号必须按 SA 连续**：一个 SA 一个序号空间，内核入向 anti-replay 窗口会把重复序号全丢。新树每个 window 在 open 时把序号重置为 1，导致「patch #1 之后第二段（vendor）全部被丢」。修法：`PageCacheWrite::adopt_sequence()`，且**必须在 bring-up 之后调用**（`open_doors` 会重置为 1）。旧树是把 `next_seq` 在多个 context 之间来回传递的（`real_ops.cpp:247/307`），本轮补上这条语义。
2. **block 0 的"校验通过"是假阳性**：目标与源都是 aarch64 ELF，头 16 字节必然相同；真判据是 offset `0x10`（`e_type`：模块 `0100` = ET_REL，动态库 `0300` = ET_DYN）。两次误判都由此产生，现已用 `0x10` 作为诊断点。
3. **helper 的调用约定（实测）**：argv = `{crashdump64, <offset>, <path>}`（两个参数即可，多余参数会让它不输出）；**stdout 必须是管道**（它用 splice 而不是拷贝）；它在 `u:r:crash_dump` 域里**能读 `/vendor`、不能读 `/data/local/tmp`**（所以用 ko 当路径测会输出空，这是域限制而非参数问题）。
4. **临时启动器每轮给一份新 SA**：同一 SA 上重复跑必须继续序号；换新 SA 则不依赖该状态（锁屏下 `am start` 即可，见 TempLaunchActivity）。

### 尚未完成（下一批）

- **hook 链**：libc++ sentry 站点规划（符号 vaddr `0xb70a8` = 文件 `0xb30a8`，其上 `bti c`，补丁点 `0xb30ac`）+ shellcode 写入（≤2520 B）+ trigger（工具已实现）+ 等待（工具已实现）⇒ 才能让 `insmod` 真正发生、模块进内核。

---

## 第三轮（同日）：sentry hook + 触发 ⇒ **全链 PASS，设备取得 root**

| 判据 | 结果 |
| --- | --- |
| hook 站点 | phase 5 site ok, 0x14012d83 at 0xb30ac (block 0xb30a0+12 verified)（守卫 bti c 在站点前 4 字节，被替换字 f9400028） |
| hook 载荷 | phase 5 payload ok, 480 bytes at file 0xfe628 (vaddr 0x102628), entry 0x90 |
| 触发与终态 | phase 4 ok, /dev/dfm0 present after 1520 ms |
| **root 判据三连** | su -c id → uid=0 context=u:r:ksu:s0；su -c getenforce → Enforcing；grep -c kernelsu /proc/modules → 1 |
| **ls / 以 root 打印** | su -c ls / → acct apex bin bootstrap-apex bugreports cache config d … |

链路（本轮）：临时启动器建 SA → 写会话帧 → 工具经**新 kernel access**：patch #1（helper→crash_dump64）→ 模块 769 块写入 vendor 载体（桥）→ sentry hook（站点字 + 480 B 载荷，含被替换字与回跳分支）→ double-fork 触发 → LKM 加载 → permissive → UMH 跑 ksud late-load → KernelSU 进内核 → 恢复 Enforcing → 标记 /dev/dfm0 与 /data/local/tmp/.ghostlock_lkm_ok。

### 本轮测出并修掉的问题

1. **守卫在被替换字之前**：bti c 位于符号处（file 0xb30a8），站点是被替换指令（0xb30ac）。首版把两者搞混，工具当场具名拒绝（打印了找到的字）——现已按「守卫在站点前 4 字节 + 站点字必须等于计划里的被替换字」两项校验。
2. **序列号必须一路传下去**：三段 stack（patch #1 → 模块 → hook）各自 open_doors 会把序号重置为 1，而会话窗口已经在 860 ⇒ 第三段全被 anti-replay 丢。现按 A→B→C 依次转交（spent_sequence = window.next_seq()）。这条已由 pagecache_write_test 的用例钉住。

### 尚未完成

- hook 计划的数值目前是**声明值**（provenance 与推导命令写在 hook/sentry_hook.cpp）；旧树的运行期推导（steps/elf_hook.cpp：ELF 解析 + 符号查找 + 落位计算）未移植，换镜像需要换计划。
- 目标字面上的入口仍是 main（现由开发工具驱动，机制与判据同）；43499 侧的根判据尚未做。
