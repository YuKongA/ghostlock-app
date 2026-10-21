# CVE-2026-43499 legacy select — non-zero FakePage AVD crash

- Time: 2026-10-20 18:56 America/Toronto
- Device: GhostLock_6_6 (Pixel_5 AVD) - Android 15, kernel 6.6.30
- Test: com.ghostlock.app.NativeSelectLegacyInstrumentedTest#legacySelectWithPreparedFakePage
- Result: device went offline/rebooted during the test; no result blob
- Repeat 19:05 with a single AVD and `-show-kernel`: same `adb: device offline`; qemu stayed alive and the AVD reconnected, but the kernel log file contains no panic/Oops line. The cause is therefore not established from logs.

## What ran

NativePrepare43499Port -> NativePrepareResult.Ok -> Cve43499FakePage.fromPrepared -> NativeSelectLegacyJni.run(target=0, mode=Zero, preserve=true, fake).

## Observed

Gradle `connectedDebugAndroidTest` reported `adb: device offline` while uninstalling; the AVD later came back with `sys.boot_completed=1`. No `GhostlockSelectLegacy` result line was captured. This is consistent with a kernel panic/reboot, but the AVD does not expose dmesg without root.

## Meaning

The non-zero FakePage path reaches a dangerous native execution path; it is not a verified route success. Do not treat this as support.

## Next

- cold boot with pstore/dmesg access;
- capture panic/reboot evidence;
- only then decide whether the route or the fake page layout is wrong.
