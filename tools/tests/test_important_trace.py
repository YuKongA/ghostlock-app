#!/usr/bin/env python3
"""Unit tests for tools/important_trace.py (host-only, no binary needed)."""
import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE = os.path.normpath(os.path.join(HERE, "..", "important_trace.py"))
_SPEC = importlib.util.spec_from_file_location("important_trace", SOURCE)
it = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(it)


def ins(addr, text):
    return it.Instr(addr, text)


class OperandTests(unittest.TestCase):
    def test_split_memory_commas(self):
        self.assertEqual(it.split_operands("[x1, #8], x2"), ["[x1, #8]", "x2"])

    def test_reg_alias(self):
        self.assertEqual(it.canon_reg("w3"), "x3")
        self.assertEqual(it.canon_reg("fp"), "x29")
        self.assertIsNone(it.canon_reg("#8"))

    def test_memory_offset(self):
        mem = it.mem_operand("[x1, #0x18]")
        self.assertEqual(mem["base"], "x1")
        self.assertEqual(mem["offset"], 0x18)


class TraceTests(unittest.TestCase):
    def test_fixed_then_add(self):
        body = [ins(0x1000, "mov x0, #1"), ins(0x1004, "add x0, x0, #2")]
        trace = it.trace_register(body, 1, "x0")
        self.assertEqual(trace["kind"], "computed")
        self.assertEqual(trace["value"], "add")
        self.assertEqual(len(trace["children"]), 2)
        self.assertEqual(trace["children"][0]["kind"], "fixed")
        self.assertEqual(trace["children"][0]["value"], "0x1")

    def test_incoming_argument(self):
        body = [ins(0x1000, "mov x0, x1")]
        trace = it.trace_register(body, 0, "x0")
        self.assertEqual(trace["kind"], "arg")
        self.assertEqual(trace["value"], "x1")

    def test_load_keeps_base_chain(self):
        body = [ins(0x1000, "ldr x0, [x1, #8]")]
        trace = it.trace_register(body, 0, "x0")
        self.assertEqual(trace["kind"], "load")
        self.assertEqual(trace["value"], "[x1, #8]")
        self.assertEqual(trace["children"][0]["kind"], "arg")

    def test_call_return(self):
        body = [ins(0x1000, "bl 0x2000 <futex_op>"), ins(0x1004, "mov x1, x0")]
        trace = it.trace_register(body, 1, "x1")
        self.assertEqual(trace["kind"], "call-return")
        self.assertEqual(trace["value"], "futex_op")

    def test_syscall_return(self):
        body = [ins(0x1000, "svc #0"), ins(0x1004, "mov x2, x0")]
        trace = it.trace_register(body, 1, "x2")
        self.assertEqual(trace["kind"], "syscall-return")


class ImportantTests(unittest.TestCase):
    def test_syscall_args(self):
        body = [ins(0x1000, "mov x8, #1"), ins(0x1004, "mov x0, #2"),
                ins(0x1008, "svc #0")]
        found = it.important_for_function("f", body, set(), 64, False)
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["kind"], "syscall")
        self.assertEqual(found[0]["args"]["x8"]["value"], "0x1")
        self.assertEqual(found[0]["args"]["x0"]["value"], "0x2")

    def test_direct_call_filter(self):
        body = [ins(0x1000, "bl 0x2000 <printf@plt>"),
                ins(0x1004, "bl 0x2004 <futex_op>")]
        found = it.important_for_function("f", body, it.DEFAULT_SYMBOLS, 64, False)
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0]["target"], "futex_op")


class CompareTests(unittest.TestCase):
    def test_same_trace_passes(self):
        body = [ins(0x1000, "mov x0, #1"), ins(0x1004, "svc #0")]
        a = {"functions": {"f": it.important_for_function("f", body, set(), 64, False)}}
        b = {"functions": {"f": it.important_for_function("f", body, set(), 64, False)}}
        ok, _ = it.compare(a, b)
        self.assertTrue(ok)

    def test_changed_root_fails(self):
        a_body = [ins(0x1000, "mov x0, #1"), ins(0x1004, "svc #0")]
        b_body = [ins(0x1000, "mov x0, #2"), ins(0x1004, "svc #0")]
        a = {"functions": {"f": it.important_for_function("f", a_body, set(), 64, False)}}
        b = {"functions": {"f": it.important_for_function("f", b_body, set(), 64, False)}}
        ok, lines = it.compare(a, b)
        self.assertFalse(ok)
        self.assertTrue(any("DIFF" in line for line in lines))


class ExtendedTraceTests(unittest.TestCase):
    def test_xzr_is_fixed_zero(self):
        body = [ins(0x1000, "mov x0, xzr")]
        trace = it.trace_register(body, 0, "x0")
        self.assertEqual(trace["kind"], "fixed")
        self.assertEqual(trace["value"], "0x0")

    def test_movk_combines_previous_movz(self):
        body = [ins(0x1000, "movz x0, #1"), ins(0x1004, "movk x0, #2, lsl #16")]
        trace = it.trace_register(body, 1, "x0")
        self.assertEqual(trace["kind"], "fixed")
        self.assertEqual(trace["value"], "0x20001")

    def test_ldp_second_destination_offset(self):
        body = [ins(0x1000, "ldp x0, x1, [x2, #0x10]")]
        trace = it.trace_register(body, 0, "x1")
        self.assertEqual(trace["kind"], "load")
        self.assertEqual(trace["value"], "[x2, #24]")

    def test_csel_is_computed(self):
        body = [ins(0x1000, "csel x0, x1, x2, eq")]
        trace = it.trace_register(body, 0, "x0")
        self.assertEqual(trace["kind"], "computed")
        self.assertEqual(trace["value"], "csel")
        self.assertEqual(len(trace["children"]), 2)

    def test_ignore_new_function(self):
        body = [ins(0x1000, "mov x0, #1"), ins(0x1004, "svc #0")]
        a = {"functions": {"f": it.important_for_function("f", body, set(), 64, False)}}
        b = {"functions": {"f": it.important_for_function("f", body, set(), 64, False),
                          "g": it.important_for_function("g", body, set(), 64, False)}}
        ok, _ = it.compare(a, b, ignore_new=True)
        self.assertTrue(ok)


if __name__ == "__main__":
    unittest.main()
