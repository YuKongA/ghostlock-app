# 5.10 multicast probe（2026-10-22）

设备：

```text
b4541958 / mondrian
5.10.252-gki-gabcbe4b2bc10
```

## 固定 profile

来源：`/sdcard/Download/k60-mondrian-20260531-full.conf`。
`temp_mcast_510.c` 固定：

```text
mcast_waiter_off=96
buffer=264 task=48 lock=56
fake_lock=4608 fake_task=12800
slots=128 count=12 stride=8
compact_waiter=1
```

## 验证

只启动 consumer，然后：

```text
do_kernel5_fake_lock_route(s, target=0x1000)
```

结果：

```text
prepare_kernel_page ok
consumer thread running on cpu=1
mcast probe done step=0 errno=0 calls=1 success=1
```

## 非 inline k5 helper

把以下两个改为普通 static：

```text
k5_build_mcast_stamp
k5_send_mcast_stamp
```

5.10 multicast probe 仍为：

```text
step=0 calls=1 success=1
```

说明 multicast 路径的非 inline 拆分通过 5.10 真机验证。
