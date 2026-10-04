#!/usr/bin/env bash
# Cross-check avbcheck against the reference python descriptor verifier on a
# synthetic avbtool-generated fixture.  Requires AVBCHECK_HOST, AVBTOOL and
# PY_VERIFY (set by the Makefile).
set -euo pipefail

HOST="${AVBCHECK_HOST:?AVBCHECK_HOST must be set}"
AVBTOOL="${AVBTOOL:?AVBTOOL must be set}"
PY_VERIFY="${PY_VERIFY:?PY_VERIFY must be set}"

fail() {
  echo "CROSSCHECK=FAIL: $*" >&2
  exit 1
}

command -v python3 >/dev/null || fail "python3 not found"
[ -f "$AVBTOOL" ] || fail "avbtool not found: $AVBTOOL"
[ -f "$PY_VERIFY" ] || fail "python verifier not found: $PY_VERIFY"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# 1) deterministic target images (same recipe as the fixture docs)
python3 - "$WORK" <<'PY'
import sys
work = sys.argv[1]
n = 4096 * 5 + 1234
open(work + "/system.img", "wb").write(
    bytes(((i * 2654435761) >> 13) & 0xFF for i in range(n)))
open(work + "/boot.img", "wb").write(
    bytes(((i * 1103515245 + 12345) >> 7) & 0xFF for i in range(300000)))
PY

# 2) avbtool fixtures: hashtree + hash footers, then a vbmeta that includes them
python3 "$AVBTOOL" add_hashtree_footer --image "$WORK/system.img" \
  --partition_name system --partition_size 1048576 --salt 0f12 \
  --hash_algorithm sha256 --do_not_generate_fec >/dev/null
python3 "$AVBTOOL" add_hash_footer --image "$WORK/boot.img" \
  --partition_name boot --partition_size 1048576 --salt 0f12 \
  --hash_algorithm sha256 >/dev/null
python3 "$AVBTOOL" make_vbmeta_image \
  --include_descriptors_from_image "$WORK/system.img" \
  --include_descriptors_from_image "$WORK/boot.img" \
  --output "$WORK/vbmeta.img" --algorithm NONE >/dev/null

# 3) reference verifier: capture the recomputed hashtree root (column "got")
python3 "$PY_VERIFY" --avbtool "$AVBTOOL" --image-dir "$WORK" \
  --tsv "$WORK/py.tsv" --vbmeta "$WORK/vbmeta.img" >"$WORK/py.out"
PY_ROOT="$(python3 - "$WORK/py.tsv" <<'PY'
import sys
for line in open(sys.argv[1]):
    if line.startswith("#"):
        continue
    c = line.rstrip("\n").split("\t")
    if len(c) >= 6 and c[1] == "hashtree" and c[2] == "system":
        print(c[5])
        break
PY
)"
[ -n "$PY_ROOT" ] || fail "python verifier produced no hashtree root"

# 4) avbcheck on the same fixture
"$HOST" avb-verify --image-dir "$WORK" --vbmeta "$WORK/vbmeta.img" \
  --no-chain --show-digests >"$WORK/our.out"
grep -q '^AVB_VERIFY=OK$' "$WORK/our.out" || fail "avbcheck did not report OK"
OUR_ROOT="$(awk '/^[[:space:]]*# digest hashtree system /{print $NF}' "$WORK/our.out" | head -n1)"
[ -n "$OUR_ROOT" ] || fail "avbcheck produced no hashtree root"

echo "python hashtree system: $PY_ROOT"
echo "avbcheck hashtree system: $OUR_ROOT"
[ "$PY_ROOT" = "$OUR_ROOT" ] || fail "hashtree roots differ"
echo "CROSSCHECK_HASHTREE=IDENTICAL"

# also the hash descriptor must match
grep -q '\[ok\]   hash boot ' "$WORK/our.out" || fail "hash boot not verified"

# 5) signed vbmeta: our raw-RSA verifier must accept a real avbtool signature
if command -v openssl >/dev/null; then
  openssl genrsa -out "$WORK/key.pem" 4096 2>/dev/null
  python3 "$AVBTOOL" make_vbmeta_image \
    --include_descriptors_from_image "$WORK/system.img" \
    --include_descriptors_from_image "$WORK/boot.img" \
    --output "$WORK/vbmeta_signed.img" --algorithm SHA256_RSA4096 \
    --key "$WORK/key.pem" >/dev/null
  "$HOST" avb-verify --image-dir "$WORK" --vbmeta "$WORK/vbmeta_signed.img" \
    --no-chain >"$WORK/signed.out"
  grep -q '\[sig-ok\]' "$WORK/signed.out" || fail "signature not verified"
  echo "CROSSCHECK_SIGNATURE=OK"
else
  echo "CROSSCHECK_SIGNATURE=SKIP (openssl not found)"
fi

# 6) negative control: a single flipped data byte must be detected
python3 - "$WORK/system.img" <<'PY'
import sys
p = sys.argv[1]
d = bytearray(open(p, "rb").read())
d[100] ^= 0x5A
open(p, "wb").write(d)
PY
set +e
"$HOST" avb-verify --image-dir "$WORK" --vbmeta "$WORK/vbmeta.img" \
  --no-chain >"$WORK/tampered.out" 2>&1
rc=$?
set -e
[ "$rc" -ne 0 ] || fail "tampered image was not rejected"
grep -q '\[FAIL\] hashtree system' "$WORK/tampered.out" ||
  fail "tampered image did not report [FAIL] hashtree system"
echo "CROSSCHECK_TAMPER=DETECTED (rc=$rc)"

echo "CROSSCHECK=OK"
