# AVD profiles for remote/main

These are schema 1 HOCON profiles generated from each AVD's `kernel-ranchu` with the current extractor.
They are flattened, unverified candidates: import them into remote/main and expect its pre-execution validation to reject missing/invalid fields.

| file | AVD | kernel | extractor route |
|---|---|---|---|
| avd-5.15.119.conf | Pixel_5 (android-33 google_apis) | 5.15.119-android13 | multicast_waiter |
| avd-6.1.23.conf | GhostLock_6_1 (android-34 google_apis_playstore) | 6.1.23-android14 | tcp_zerocopy (empty geometry) |
| avd-6.6.30.conf | GhostLock_6_6 (android-35 google_apis_playstore) | 6.6.30-android15 | select_stack (waiter_shift=-2) |

## Generation

```bash
ghostlock-extract <kernel-ranchu> --format conf --out /tmp/extracted.conf
```

Then map the schema-3 `ghostlock { backend { cve_2026_43499 { ... } } }` fields to the remote/main schema-1 flat layout:
`task_struct`, `cred`, `offset`, `route`, `kernelsnitch`.

## Limits

- `avd-5.15.119.conf` only has multicast geometry; select/tcp are not emitted.
- `avd-6.1.23.conf` has an empty tcp route; pselect is not feasible on this kernel.
- `avd-6.6.30.conf` has select geometry but no tcp geometry.
- `cred.ref*_image` for 6.1/6.6 are absent in the extractor output and are omitted, not guessed.
- None of these is a device gate or a supported-profile claim.
