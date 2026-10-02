# SPDX-License-Identifier: MIT
"""Network-free target-gate refusals; never execute ARM artifacts or probe Avahi."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("mdns_target_gate", Path(__file__).with_name("target.py"))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class TargetBoundary(unittest.TestCase):
    def test_native_profile(self):
        gate.platform_check("Linux", "aarch64", "little", 8, 8, "NVIDIA_DGX_Spark\n")
        for row in (("Linux", "x86_64", "little", 8, 8, "NVIDIA_DGX_Spark"),
                    ("Linux", "aarch64", "big", 8, 8, "NVIDIA_DGX_Spark"),
                    ("Linux", "aarch64", "little", 4, 4, "NVIDIA_DGX_Spark"),
                    ("Darwin", "aarch64", "little", 8, 8, "NVIDIA_DGX_Spark"),
                    ("Linux", "aarch64", "little", 8, 8, "fixture")):
            with self.assertRaises(ValueError):
                gate.platform_check(*row)

    def test_elf_architecture_and_type(self):
        def header(kind=3, machine=183, byteorder=1):
            data = bytearray(64)
            data[:6] = b"\x7fELF\x02" + bytes([byteorder])
            struct.pack_into("<HH", data, 16, kind, machine)
            return data
        for kind in (2, 3):
            self.assertEqual(gate.elf_kind(header(kind=kind)), kind)
        for data in (header(kind=1), header(machine=62), header(machine=40),
                     header(byteorder=2), bytes(64), header()[:63]):
            with self.assertRaises(ValueError):
                gate.elf_kind(data)

    def test_exact_producer(self):
        with tempfile.TemporaryDirectory(prefix="mdns-target-alias-") as temporary:
            root = Path(temporary)
            library = root / "libsynapse-mdns.so.1.0.0"
            library.write_bytes(b"owned producer fixture")
            with self.assertRaises(ValueError):
                gate.exact_producer(library)
            alias = root / "libsynapse-mdns.so.1"
            alias.write_bytes(b"wrong producer")
            with self.assertRaises(ValueError):
                gate.exact_producer(library)
            alias.unlink()
            alias.symlink_to(library.name)
            gate.exact_producer(library)

    def test_import_is_exact(self):
        self.assertTrue(gate.imported("(NEEDED) Shared library: [libsynapse-mdns.so.1]", "libsynapse-mdns.so.1"))
        for row in ("(SONAME) Library soname: [libsynapse-mdns.so.1]",
                    "(NEEDED) Shared library: [libsynapse-mdns.so.10]", ""):
            self.assertFalse(gate.imported(row, "libsynapse-mdns.so.1"))


unittest.main()
