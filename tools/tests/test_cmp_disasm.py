#!/usr/bin/env python3
"""Self-tests for the relocation normalisation in tools/cmp_disasm.py.

Run:
    python3 -m unittest discover -s tools/tests -p "test_*.py"
    python3 tools/tests/test_cmp_disasm.py
"""
import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.normpath(os.path.join(HERE, "..", "cmp_disasm.py"))
_SPEC = importlib.util.spec_from_file_location("cmp_disasm", SOURCE)
cd = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(cd)

RO = 0x2
RW = 0x3
DATUM = bytes([0x02, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00])
OTHER = bytes([0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])


class FakeElf:
    """ELF stand-in: read(vaddr, size) -> (name, bytes, flags) or None."""

    def __init__(self, regions):
        self.regions = regions

    def read(self, vaddr, size):
        for name, addr, data, flags in self.regions:
            if addr <= vaddr and vaddr + size <= addr + len(data):
                start = vaddr - addr
                return name, data[start:start + size], flags
        return None


class EffectiveAddressTests(unittest.TestCase):
    def test_offset_is_added_to_page(self):
        body = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]"]
        ops = cd.memory_operands(body[1], cd.track_bases(body)[1])
        self.assertEqual(ops[0]["eff"], 0xB698)

    def test_self_add_folds_into_base(self):
        body = ["adrp x8, 0x1000", "add x8, x8, #0x100", "ldr x1, [x8, #0x8]"]
        ops = cd.memory_operands(body[2], cd.track_bases(body)[2])
        self.assertEqual(ops[0]["eff"], 0x1108)

    def test_self_sub_folds_negatively(self):
        body = ["adrp x8, 0x2000", "sub x8, x8, #0x100", "ldr x1, [x8]"]
        ops = cd.memory_operands(body[2], cd.track_bases(body)[2])
        self.assertEqual(ops[0]["eff"], 0x1F00)

    def test_overwrite_drops_stale_page(self):
        body = ["adrp x9, 0x1000", "mov x9, x0", "ldr x1, [x9]"]
        self.assertEqual(cd.memory_operands(body[2], cd.track_bases(body)[2]), [])


class RelocationResolveTests(unittest.TestCase):
    def resolve(self, base, cur, base_elf=None, cur_elf=None):
        return cd.relocation_resolve(base, cur, base_elf, cur_elf)

    def test_equal_effective_address_is_normalised(self):
        base = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]"]
        cur = ["adrp x9, 0xb698", "ldr d1, [x9]"]
        _, _, notes, _ = self.resolve(base, cur)
        self.assertEqual(len(notes), 1)
        self.assertEqual(notes[0][3][0][2], "same-address")

    def test_relocated_identical_readonly_datum_is_normalised(self):
        base = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]"]
        cur = ["adrp x9, 0xc000", "ldr d1, [x9]"]
        elf_b = FakeElf([(".rodata", 0xB698, DATUM, RO)])
        elf_c = FakeElf([(".rodata", 0xC000, DATUM, RO)])
        _, _, notes, _ = self.resolve(base, cur, elf_b, elf_c)
        self.assertEqual(len(notes), 1)
        self.assertEqual(notes[0][3][0][2], "same-readonly-datum")

    def test_different_datum_is_not_normalised(self):
        base = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]"]
        cur = ["adrp x9, 0xc000", "ldr d1, [x9]"]
        elf_b = FakeElf([(".rodata", 0xB698, DATUM, RO)])
        elf_c = FakeElf([(".rodata", 0xC000, OTHER, RO)])
        nb, nc, notes, _ = self.resolve(base, cur, elf_b, elf_c)
        self.assertEqual(notes, [])
        self.assertEqual(nb, base)
        self.assertEqual(nc, cur)

    def test_writable_target_is_not_normalised(self):
        base = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]"]
        cur = ["adrp x9, 0xc000", "ldr d1, [x9]"]
        elf_b = FakeElf([(".data", 0xB698, DATUM, RW)])
        elf_c = FakeElf([(".data", 0xC000, DATUM, RW)])
        _, _, notes, _ = self.resolve(base, cur, elf_b, elf_c)
        self.assertEqual(notes, [])

    def test_store_to_different_address_is_not_normalised(self):
        base = ["adrp x9, 0xb000", "str d1, [x9, #0x698]"]
        cur = ["adrp x9, 0xc000", "str d1, [x9]"]
        elf_b = FakeElf([(".rodata", 0xB698, DATUM, RO)])
        elf_c = FakeElf([(".rodata", 0xC000, DATUM, RO)])
        _, _, notes, _ = self.resolve(base, cur, elf_b, elf_c)
        self.assertEqual(notes, [])

    def test_missing_adrp_partner_is_not_normalised(self):
        base = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]"]
        cur = ["nop", "ldr d1, [x9]"]
        _, _, notes, _ = self.resolve(base, cur)
        self.assertEqual(notes, [])

    def test_unrelated_immediate_change_is_left_alone(self):
        base = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]", "mov w0, #1"]
        cur = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]", "mov w0, #2"]
        nb, nc, notes, _ = self.resolve(base, cur)
        self.assertEqual(notes, [])
        self.assertEqual(nb, base)
        self.assertEqual(nc, cur)


    def test_same_shape_readonly_different_datum_is_hard(self):
        base = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]"]
        cur = ["adrp x9, 0xc000", "ldr d1, [x9, #0x8]"]
        elf_b = FakeElf([(".rodata", 0xB698, DATUM, RO)])
        elf_c = FakeElf([(".rodata", 0xC008, OTHER, RO)])
        _, _, notes, hard = self.resolve(base, cur, elf_b, elf_c)
        self.assertEqual(notes, [])
        self.assertEqual(hard, [1])

    def test_same_shape_writable_different_datum_is_legacy(self):
        base = ["adrp x9, 0xb000", "ldr d1, [x9, #0x698]"]
        cur = ["adrp x9, 0xc000", "ldr d1, [x9, #0x8]"]
        elf_b = FakeElf([(".data", 0xB698, DATUM, RW)])
        elf_c = FakeElf([(".data", 0xC008, OTHER, RW)])
        _, _, notes, hard = self.resolve(base, cur, elf_b, elf_c)
        self.assertEqual(notes, [])
        self.assertEqual(hard, [])


if __name__ == "__main__":
    unittest.main()
