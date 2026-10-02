# SPDX-License-Identifier: MIT
"""Explicit native DGX Spark artifact check, not an all-Arm ISA-baseline proof."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import struct
import subprocess
import sys

ENV = dict(os.environ, LC_ALL="C")
ENV.pop("LD_PRELOAD", None)
ENV.pop("LD_LIBRARY_PATH", None)


def output(argv, env=ENV):
    result = subprocess.run(argv, env=env, capture_output=True, text=True, timeout=30)
    if result.returncode or len(result.stdout) + len(result.stderr) > 262144:
        raise ValueError("target inspection/execution failed: " + str(argv[0]) + ": exit=" + str(result.returncode))
    return result.stdout


def platform_check(system, machine, byteorder, long_size, pointer_size, product):
    if (system, machine, byteorder, long_size, pointer_size) != ("Linux", "aarch64", "little", 8, 8):
        raise ValueError("dgx-spark requires native Linux AArch64 little-endian LP64")
    if product.strip() != "NVIDIA_DGX_Spark":
        raise ValueError("dgx-spark requires an explicitly identified DGX Spark target")


def elf_kind(header):
    if len(header) != 64 or header[:6] != b"\x7fELF\x02\x01":
        raise ValueError("target artifact must be little-endian ELF64")
    kind, machine = struct.unpack_from("<HH", header, 16)
    if machine != 183 or kind not in (2, 3):
        raise ValueError("target artifact must be linked AArch64 ELF")
    return kind


def inspect(path):
    path = Path(path).resolve(strict=True)
    with path.open("rb") as source:
        kind = elf_kind(source.read(64))
    program = output(["readelf", "-lW", str(path)])
    dynamic = output(["readelf", "-dW", str(path)])
    stack = next((line for line in program.splitlines() if "GNU_STACK" in line), "")
    if not stack or "RWE" in stack or "GNU_RELRO" not in program or "BIND_NOW" not in dynamic:
        raise ValueError("target artifact lacks RELRO/NOW/NX")
    dso = kind == 3 and "INTERP" not in program
    if "(RPATH)" in dynamic or (dso and "(RUNPATH)" in dynamic):
        raise ValueError("shipping/runtime search path ambiguity")
    return path, dso, dynamic


def imported(dynamic, name):
    return re.search(r"\(NEEDED\).*\[" + re.escape(name) + r"\]", dynamic) is not None


def exact_producer(library):
    soname = library.parent / "libsynapse-mdns.so.1"
    if not soname.is_file() or hashlib.sha256(soname.read_bytes()).digest() != hashlib.sha256(library.read_bytes()).digest():
        raise ValueError("selected SONAME alias must match the audited producer")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=["dgx-spark"], required=True)
    parser.add_argument("library")
    parser.add_argument("consumers", nargs=3, help="public shared/static callers and network-free mechanical fixture")
    args = parser.parse_args()
    platform_check(platform.system(), platform.machine(), sys.byteorder,
                   ctypes.sizeof(ctypes.c_long), ctypes.sizeof(ctypes.c_void_p),
                   Path("/sys/class/dmi/id/product_name").read_text())
    library, dso, dynamic = inspect(args.library)
    if not dso or not re.search(r"\(SONAME\).*\[libsynapse-mdns\.so\.1\]", dynamic):
        raise ValueError("target producer must be API/ABI1 shared library")
    exact_producer(library)
    runtime = dict(ENV, LD_LIBRARY_PATH=str(library.parent))
    consumers = [inspect(path) for path in args.consumers]
    if any(dso for _, dso, _ in consumers) or not imported(consumers[0][2], "libsynapse-mdns.so.1"):
        raise ValueError("target requires an executable importing the audited producer")
    if imported(consumers[1][2], "libsynapse-mdns.so.1") or imported(consumers[2][2], "libavahi-client.so.3"):
        raise ValueError("static caller/mechanical fixture dependency boundary changed")
    # ldd observes only these owned, source-built callers. Controlled library
    # selection must resolve the producer rather than another installed SDK.
    selected = output(["ldd", str(consumers[0][0])], runtime)
    match = re.search(r"libsynapse-mdns\.so\.1\s+=>\s+(\S+)", selected)
    if not match or Path(match.group(1)).resolve() != library:
        raise ValueError("target caller did not select the audited producer")
    records = []
    for path, _, _ in consumers:
        print(output([str(path)], runtime).strip())
        records.append({"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "exit": 0})
    print(json.dumps({"schema": "synapse.mdns.target-check/v1", "profile": args.profile,
                      "architecture": "aarch64", "abi": "little-endian-lp64", "apiMajor": 1,
                      "producerSha256": hashlib.sha256(library.read_bytes()).hexdigest(),
                      "consumers": records, "evidence": "native-target-exercised-paths"}, sort_keys=True))
    print("DGX Spark native target/ABI1 artifact execution: PASS; not general Armv8-A baseline or physical mDNS proof")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print("DGX Spark target unqualified: " + str(error), file=sys.stderr)
        sys.exit(1)
