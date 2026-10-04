#!/usr/bin/env python3
"""GhostLock AVB descriptor verifier (integrity, no signing key required).

For every AVB descriptor contained in the given vbmeta images this helper
recomputes the *stored digest* from the retained partition image and compares
it with the descriptor:

* Hash descriptor     -> sha256(salt || image[0:image_size])          (flat)
* Hashtree descriptor -> Merkle root over the data region using the same
                         algorithm, salt, block size and digest padding.

No private/public key is involved: this is exactly the integrity half of AVB.
Authenticity (the vbmeta signature) is covered separately by
`avbtool verify_image`, which the shell exposes through `avb-verify --verify`.

The verifier imports the avbtool module from --avbtool so the descriptor
struct layout, the hash-tree generator and the padding rules are byte-for-byte
the ones the images were produced with.

Exit codes:
  0  every descriptor that has a retained image matches
  1  at least one mismatch or hard error
  2  usage/setup error (bad paths, cannot import avbtool)
"""

import argparse
import hashlib
import importlib.util
import os
import sys
from importlib.machinery import SourceFileLoader

# Do not leave __pycache__ inside the vendored third_party/avbtool directory.
sys.dont_write_bytecode = True

TAB = chr(9)


def load_avbtool(path):
    # The vendored single-file avbtool has no .py suffix, so a SourceFileLoader
    # must be supplied explicitly (spec_from_file_location alone returns None).
    loader = SourceFileLoader("glk_vendored_avbtool", path)
    spec = importlib.util.spec_from_loader(loader.name, loader)
    if spec is None:
        raise RuntimeError("cannot load avbtool from %s" % path)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def find_target(image_dir, names):
    for name in names:
        cand = os.path.join(image_dir, name + ".img")
        if os.path.isfile(cand):
            return cand
    return None


def flat_digest(path, size, algorithm, salt):
    """AVB hash descriptor digest: H(salt || image[0:size])."""
    hasher = hashlib.new(algorithm)
    hasher.update(salt)
    remaining = size
    with open(path, "rb") as fh:
        while remaining > 0:
            chunk = fh.read(min(1 << 20, remaining))
            if not chunk:
                break
            hasher.update(chunk)
            remaining -= len(chunk)
    if remaining != 0:
        raise IOError("short read: %d byte(s) missing" % remaining)
    return hasher.hexdigest()


def hashtree_digest(avb_mod, path, desc):
    """AVB hashtree descriptor root digest, recomputed from the data region."""
    digest_size = desc._hashtree_digest_size()
    digest_padding = avb_mod.round_to_pow2(digest_size) - digest_size
    offsets, tree_size = avb_mod.calc_hash_level_offsets(
        desc.image_size, desc.data_block_size, digest_size + digest_padding)
    image = avb_mod.ImageHandler(path, read_only=True)
    root, _ = avb_mod.generate_hash_tree(
        image, desc.image_size, desc.data_block_size, desc.hash_algorithm,
        desc.salt, digest_padding, offsets, tree_size)
    return root.hex()


def slot_for(vbmeta_path):
    stem = os.path.basename(vbmeta_path)
    if stem.endswith(".img"):
        stem = stem[:-4]
    if stem.endswith("_a"):
        return "_a"
    if stem.endswith("_b"):
        return "_b"
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--avbtool", required=True,
                    help="path to the (vendored) single-file avbtool")
    ap.add_argument("--image-dir", required=True,
                    help="directory holding <partition>.img files")
    ap.add_argument("--tsv", default="", help="optional TSV output path")
    ap.add_argument("--active-slot", default="",
                    help="slot the retained *-verity.img views belong to "
                         "(e.g. _a); required to verify hashtree descriptors "
                         "on A/B devices without false mismatches")
    ap.add_argument("--vbmeta", action="append", default=[],
                    help="vbmeta image to inspect; repeatable")
    args = ap.parse_args()

    if not os.path.isfile(args.avbtool):
        print("[ERR]  avbtool file not found: %s" % args.avbtool)
        return 2
    if not args.vbmeta:
        print("[ERR]  no --vbmeta images given")
        return 2
    try:
        avb_mod = load_avbtool(args.avbtool)
    except Exception as exc:  # noqa: BLE001
        print("[ERR]  cannot import avbtool: %s" % exc)
        return 2

    avb = avb_mod.Avb()
    image_handler = avb_mod.ImageHandler
    hash_desc = avb_mod.AvbHashDescriptor
    tree_desc = avb_mod.AvbHashtreeDescriptor

    tsv = []
    total = oks = fails = skips = errs = 0
    for vbmeta in args.vbmeta:
        base = os.path.basename(vbmeta)
        if not os.path.isfile(vbmeta):
            print("[ERR]  missing vbmeta image: %s" % vbmeta)
            errs += 1
            continue
        slot = slot_for(vbmeta)
        try:
            image = image_handler(vbmeta, read_only=True)
            _footer, _header, descriptors, _size = avb._parse_image(image)
        except Exception as exc:  # noqa: BLE001
            print("[ERR]  cannot parse %s: %s" % (base, exc))
            errs += 1
            continue
        print("### %s (slot=%s, %d descriptors)" %
              (base, slot or "-", len(descriptors)))
        for desc in descriptors:
            if isinstance(desc, hash_desc):
                name = desc.partition_name
                total += 1
                cand = [name + slot, name] if slot else [name]
                path = find_target(args.image_dir, cand)
                if path is None:
                    print("[skip] hash %s (no %s)" %
                          (name, " or ".join(c + ".img" for c in cand)))
                    skips += 1
                    tsv.append((base, "hash", name, "skip", "", "", ""))
                    continue
                try:
                    got = flat_digest(path, desc.image_size,
                                      desc.hash_algorithm, desc.salt)
                except Exception as exc:  # noqa: BLE001
                    print("[ERR]  hash %s (%s): %s" %
                          (name, os.path.basename(path), exc))
                    errs += 1
                    tsv.append((base, "hash", name, "err", "",
                                "", os.path.basename(path)))
                    continue
                exp = desc.digest.hex()
                if got == exp:
                    print("[ok]   hash %s -> %s (%s)" %
                          (name, os.path.basename(path), desc.hash_algorithm))
                    oks += 1
                    tsv.append((base, "hash", name, "ok", exp, got,
                                os.path.basename(path)))
                else:
                    print("[FAIL] hash %s expected=%s got=%s (%s)" %
                          (name, exp, got, os.path.basename(path)))
                    fails += 1
                    tsv.append((base, "hash", name, "fail", exp, got,
                                os.path.basename(path)))
            elif isinstance(desc, tree_desc):
                name = desc.partition_name
                total += 1
                pref = os.path.join(args.image_dir, name + "-verity.img")
                active = args.active_slot
                # The repo-external backups keep one *-verity.img per logical
                # partition, taken from the *active* slot.  Only use it when the
                # vbmeta slot is unknown or equals the active slot, otherwise a
                # different slot would be compared and reported as a mismatch.
                use_pref = os.path.isfile(pref) and (
                    not active or not slot or active == slot)
                if use_pref:
                    path = pref
                else:
                    cand = [name + slot] if slot else [name]
                    path = find_target(args.image_dir, cand)
                if path is None:
                    print("[skip] hashtree %s (no %s-verity.img / %s.img; "
                          "slot=%s active=%s)" %
                          (name, name, name, slot or "-", active or "-"))
                    skips += 1
                    tsv.append((base, "hashtree", name, "skip", "", "", ""))
                    continue
                try:
                    got = hashtree_digest(avb_mod, path, desc)
                except Exception as exc:  # noqa: BLE001
                    print("[ERR]  hashtree %s (%s): %s" %
                          (name, os.path.basename(path), exc))
                    errs += 1
                    tsv.append((base, "hashtree", name, "err", "",
                                "", os.path.basename(path)))
                    continue
                exp = desc.root_digest.hex()
                if got == exp:
                    print("[ok]   hashtree %s -> %s (%s)" %
                          (name, os.path.basename(path), desc.hash_algorithm))
                    oks += 1
                    tsv.append((base, "hashtree", name, "ok", exp, got,
                                os.path.basename(path)))
                else:
                    print("[FAIL] hashtree %s expected=%s got=%s (%s)" %
                          (name, exp, got, os.path.basename(path)))
                    fails += 1
                    tsv.append((base, "hashtree", name, "fail", exp, got,
                                os.path.basename(path)))

    print("# summary total=%d ok=%d fail=%d skip=%d err=%d" %
          (total, oks, fails, skips, errs))
    if args.tsv:
        with open(args.tsv, "w") as fh:
            fh.write("# columns=vbmeta%s kind%s partition%s status%s "
                     "expected%s got%s image\n" % (TAB, TAB, TAB, TAB, TAB, TAB))
            for row in tsv:
                fh.write(TAB.join(row) + "\n")
    if fails > 0 or errs > 0:
        print("AVB_VERIFY=FAIL")
        return 1
    print("AVB_VERIFY=OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
