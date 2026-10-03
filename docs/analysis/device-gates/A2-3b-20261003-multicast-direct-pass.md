# A2-3b 真机门禁：AddressSpace/ResolvedAddresses 拆分（multicast_waiter）— PASS

对应提交 `bc46c4f`（Phase A2-3b）。候选二进制 SHA-256
`e81a1ebc05166d6a55dc577a37ca45b4a72371afa2a33098670a56ae4d46bada`。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；冷启动 `boot_ms=34366`；
  KernelSU 未加载、Enforcing、uid 2000。route = multicast_waiter；CPU 对 0/1。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a3b --load-prebuilt-profile
  /data/local/tmp/p0.bin`。

## 结果

PASS（一次冷机跑通过；完整输出见 `.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[*] [T+16898ms] exploit complete
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root)`；无 panic。

## 变更说明

`memory::ResolvedAddresses` 拆为通用基类 `memory::AddressSpace`（soc/kernel_phys_load/phys_offset +
`init_for_soc`/`data_alias`/`data_alias_checked`/`phys_load`/`soc_name`）与派生 `ResolvedAddresses`
（`init_cred_image` + `init`/`init_cred_image_addr`）。**基类前置于派生**，字段顺序/大小/对齐与原来完全一致，
`session_layout_test` 的 `Cve2026_43499State` 绝对偏移不变；继承使全部既有调用点无需改动。
`init` 保留原校验顺序（uname_r + offsets.init_cred → EINVAL，越界 → ERANGE）。`cmp_disasm --reviewed` PASS。
