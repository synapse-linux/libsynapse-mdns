# SPDX-License-Identifier: MIT
"""Network-free ISA decision/parser regression tests; not a runtime receipt."""
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("synapse_isa", Path(__file__).with_name("isa.py"))
isa = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = isa
spec.loader.exec_module(isa)


class GateTests(unittest.TestCase):
    def case(self, dso, needed, used):
        return isa.Artifact(Path("/fixture"), dso, needed, used, "")

    def test_needed_and_used_are_not_interchangeable(self):
        self.assertFalse(isa.needs_execution(self.case(False, isa.BASELINE, "x86-64-v4")))
        self.assertFalse(isa.needs_execution(self.case(True, isa.BASELINE, None)))
        self.assertTrue(isa.needs_execution(self.case(True, None, isa.BASELINE)))
        for value in (self.case(False, None, isa.BASELINE), self.case(True, None, None),
                      self.case(True, None, "x86-64-v2"), self.case(False, "x86-64-v3", isa.BASELINE),
                      self.case(True, "x86-64-baseline, x86-64-v2", isa.BASELINE)):
            with self.assertRaises(ValueError):
                isa.needs_execution(value)

    def test_wrong_architecture_or_unlinked_input(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "artifact"
            for machine, kind, magic in ((183, 3, b"\x7fELF\x02\x01"), (62, 1, b"\x7fELF\x02\x01"),
                                         (62, 3, b"\x7fELF\x01\x01")):
                h = bytearray(64)
                h[:6] = magic
                struct.pack_into("<HH", h, 16, kind, machine)
                path.write_bytes(h)
                with self.assertRaises(ValueError):
                    isa.inspect(path)

    def test_pie_and_dso_have_different_requirements(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "artifact"
            h = bytearray(64);h[:6] = b"\x7fELF\x02\x01"
            struct.pack_into("<HH", h, 16, 3, 62)
            path.write_bytes(h)
            for program, dso in (("LOAD\nINTERP\n", False), ("LOAD\n", True)):
                with patch.object(isa, "output", side_effect=[program, "x86 ISA used: x86-64-baseline\n", ""]):
                    a = isa.inspect(path)
                self.assertEqual(a.dso, dso)
                if dso:
                    self.assertTrue(isa.needs_execution(a))
                else:
                    with self.assertRaises(ValueError):
                        isa.needs_execution(a)

    def test_no_artifact_or_missing_execution_inputs_is_not_success(self):
        with patch.object(sys, "argv", ["isa.py"]), contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as error:
                isa.main()
            self.assertEqual(error.exception.code, 2)
        with patch.object(sys, "argv", ["isa.py", "--emulator", "/qemu", "/fixture"]), \
             patch.object(isa, "inspect", return_value=self.case(False, isa.BASELINE, None)):
            with self.assertRaises(ValueError):
                isa.main()
        with patch.object(sys, "argv", ["isa.py", "/fixture"]), \
             patch.object(isa, "inspect", return_value=self.case(True, None, isa.BASELINE)):
            with self.assertRaises(ValueError):
                isa.main()

    def test_pending_dso_without_matching_consumer_never_executes(self):
        dso = isa.Artifact(Path('/producer'), True, None, isa.BASELINE,
                           '(SONAME) Library soname: [libfixture.so.1]')
        exe = self.case(False, isa.BASELINE, None)
        with tempfile.TemporaryDirectory() as root, \
             patch.object(sys, 'argv', ['isa.py', '--emulator', '/qemu', '--sysroot', root,
                                       '--cpu-probe', '/probe', '--library-path', '/sdk', '/producer', '/consumer']), \
             patch.object(isa, 'inspect', side_effect=[dso, exe, exe]), \
             patch.object(isa.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'no declared executable consumer'):
                isa.main()
            run.assert_not_called()

    def test_changed_guest_producer_never_executes(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name);producer = root/'producer'
            producer.write_bytes(b'qualified producer')
            guest = root/'sdk/libfixture.so.1';guest.parent.mkdir()
            guest.write_bytes(b'different producer')
            dso = isa.Artifact(producer, True, None, isa.BASELINE,
                               '(SONAME) Library soname: [libfixture.so.1]')
            exe = isa.Artifact(Path('/consumer'), False, isa.BASELINE, None,
                               '(NEEDED) Shared library: [libfixture.so.1]')
            with patch.object(sys, 'argv', ['isa.py', '--emulator', '/qemu', '--sysroot', str(root),
                                          '--cpu-probe', '/probe', '--library-path', '/sdk', '/producer', '/consumer']), \
                 patch.object(isa, 'inspect', side_effect=[dso, exe, exe]), \
                 patch.object(isa.subprocess, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'guest DSO does not match audited artifact'):
                    isa.main()
                run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
