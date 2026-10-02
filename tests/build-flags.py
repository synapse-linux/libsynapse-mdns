# SPDX-License-Identifier: MIT
"""Compiler-target flags regression. Dry runs, no compilation or network."""
import os
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parent.parent
env = dict(os.environ, LC_ALL="C")
for name in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES"):
    env.pop(name, None)
with tempfile.TemporaryDirectory(prefix="synapse-mdns-flags-") as temporary:
    root = Path(temporary)
    compiler = root / "compiler"
    compiler.write_text('#!/bin/sh\nif [ "$1" = -dumpmachine ]; then printf "%s\\n" "$MDNS_TEST_TRIPLE"; else exit 1; fi\n')
    compiler.chmod(0o700)
    for triple, expected in (("x86_64-linux-gnu", "-march=x86-64 -mtune=generic"),
                             ("aarch64-linux-gnu", "-march=armv8-a -mtune=generic"),
                             ("aarch64-unknown-linux-gnu", "-march=armv8-a -mtune=generic")):
        result = subprocess.run(["make", "--no-print-directory", "-rR", "-n", "all", "test",
                                 "CC=" + str(compiler), "BUILD_DIR=" + str(root / "build"),
                                 "AVAHI_CFLAGS=-I/fixture-sdk/include"],
                                cwd=source, env=dict(env, MDNS_TEST_TRIPLE=triple),
                                capture_output=True, text=True, timeout=20, check=True)
        recipe = next(line for line in result.stdout.splitlines() if "src/mdns.c" in line)
        assert expected in recipe, (triple, recipe)
        assert "-march=native" not in recipe and "-mcpu=native" not in recipe, recipe
        assert ("-march=x86-64" in recipe) == triple.startswith("x86_64"), recipe
        fixture = next(line for line in result.stdout.splitlines() if '"tests/mdns-fixture.c"' in line)
        public = next(line for line in result.stdout.splitlines() if '"tests/public-api.c"' in line)
        assert "-I/fixture-sdk/include" in fixture and "-I/fixture-sdk/include" not in public
        print(triple + ": compiler-target conservative flags and private fixture SDK: PASS")
print("mDNS compiler-target flags: x86-64 unchanged, AArch64 explicit Armv8-A: OK")
