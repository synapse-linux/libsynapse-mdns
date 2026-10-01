# SPDX-License-Identifier: MIT
"""Fail closed when a linked artifact does not declare baseline ISA needs."""
import re
import subprocess
import sys

failed = False
for artifact in sys.argv[1:]:
    header = subprocess.check_output(["readelf", "-h", artifact], text=True)
    notes = subprocess.check_output(["readelf", "-n", artifact], text=True)
    found = re.search(r"x86 ISA needed:\s*([^\n]+)", notes)
    needed = found.group(1).strip() if found else "<not declared>"
    if "X86-64" not in header or needed != "x86-64-baseline":
        print(f"ISA gate unqualified: {artifact}: x86 ISA needed={needed}", file=sys.stderr)
        failed = True
    else:
        print(f"ISA gate: {artifact}: x86-64-baseline")
if failed:
    print("Source flags/ISA-used notes alone do not qualify linked runtime; use target evidence.", file=sys.stderr)
    sys.exit(1)
