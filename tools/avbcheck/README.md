# avbcheck

On-device AVB / partition integrity checker for GhostLock device guards.
`avbcheck` is a dependency-free, statically linked aarch64 executable that
recomputes AVB hash / hashtree descriptor digests and verifies vbmeta
signatures while the data stays on the phone.  It is the on-device replacement
for the host-side `tools/device-guard/avb_verify_descriptors.py` +
`avbtool` combination, so a full check no longer needs to copy ~22 GB of
partitions to a workstation.

## Build

```sh
cd tools/avbcheck
make              # NDK aarch64 static -> build/avbcheck-aarch64
make host         # host build      -> build/avbcheck-host
make test         # host unit tests + avbtool/python cross-check
```

`make` uses `ANDROID_NDK_HOME` (or `NDK=`) and defaults to the NDK recorded
in the GhostLock docs.  The device binary is fully static (no libc++/libc .so).
C++17, `-Wall -Wextra`, zero warnings, no external libraries.

## Commands

```sh
# single streamed SHA-256 of a file or block device
avbcheck hash /dev/block/by-name/vbmeta_a
avbcheck hash by-name:super
avbcheck hash mapper:system-verity

# avb_guard.sh-compatible baseline / check, computed on the device
avbcheck baseline --serial QV770MFGJ1 vbmeta_a boot_a system-verity
avbcheck check /data/local/tmp/QV770MFGJ1-avb-baseline.tsv

# parse vbmeta and recompute every hash/hashtree descriptor + signature
avbcheck avb-verify --slot _a
avbcheck avb-verify --slot _a --vbmeta /dev/block/by-name/vbmeta_a --show-digests
# offline (host) against retained images:
avbcheck avb-verify --slot _a --image-dir DIR --vbmeta DIR/vbmeta_a.img

# dm-verity runtime health (dmctl, falls back to dmsetup) + boot state
avbcheck verity-status
```

On the device run the binary under `su` (block devices are root-only):

```sh
adb push build/avbcheck-aarch64 /data/local/tmp/avbcheck
adb shell su -c "/data/local/tmp/avbcheck avb-verify --slot _a"
```

### avb-verify behaviour

* parses the `AVB0` header and hash / hashtree / chain / property /
  kernel-cmdline descriptors from `vbmeta<slot>`;
* recomputes `H(salt || image[0:image_size])` for hash descriptors from
  the by-name device (`<name><slot>`), and the Merkle root for hashtree
  descriptors from the verified data view (`/dev/block/mapper/<name>-verity`);
* **follows chain descriptors** (boot, init_boot, recovery, vbmeta_system,
  ...): it verifies the child vbmeta signature against the public key
  embedded in the parent chain descriptor, then recurses into the child
  descriptors;
* verifies the top-level vbmeta against its own embedded public key;
* prints `[ok]` / `[FAIL]` / `[ERR]` / `[skip]` and `[sig-ok]` /
  `[sig-FAIL]` / `[sig-skip]`, exits non-zero on any failure.

The raw-RSA verifier implements the AVB PKCS#1 v1.5 layout
(`padding || digest`, digest computed over `header || auxiliary block`) with
a small in-tree big-integer modexp (`e = 65537`), so SHA-256/SHA-512
RSA-2048/4096/8192 vbmeta structures are covered without OpenSSL.

### Exit codes

| command | 0 | non-zero |
|---|---|---|
| `hash` | hashed | cannot resolve/read |
| `baseline` | entries written | no entries captured |
| `check` | `CONSISTENT` | `INCONSISTENT` (fail or read error) |
| `avb-verify` | every recomputed descriptor + signature matched | any `[FAIL]`/`[ERR]` |
| `verity-status` | no dm-verity `E` state | any `E` target |

## Tests

`make test` runs:

1. SHA-256 / SHA-512 / SHA-1 standard vectors, level-offset and Merkle-root
   vectors, and a synthetic vbmeta parse/digest test;
2. `tests/run_crosscheck.sh`: generates an avbtool fixture (hashtree + hash
   descriptors, unsigned and `SHA256_RSA4096`-signed), recomputes the
   hashtree root with both `avbcheck` and
   `tools/device-guard/avb_verify_descriptors.py` and asserts the roots are
   byte-identical; then flips one data byte and asserts it is detected.

## Files

| path | purpose |
|---|---|
| `src/sha2.h/.cpp` | SHA-256 / SHA-512 |
| `src/avb.h/.cpp` | vbmeta parsing, Merkle tree, raw RSA, SHA-1 |
| `src/main.cpp` | CLI |
| `tests/test_avbcheck.cpp` | host unit tests |
| `tests/run_crosscheck.sh` | avbtool / python byte-exact cross-check |
