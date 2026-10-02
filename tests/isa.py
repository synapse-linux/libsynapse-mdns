# SPDX-License-Identifier: MIT
"""Type-aware ELF ISA gate; an undeclared DSO needs explicit baseline execution."""
import argparse
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
from typing import Optional

BASELINE = "x86-64-baseline"
# qemu64 defaults include some post-baseline instructions: explicitly remove them.
CPU = "qemu64,-pni,-ssse3,-sse4.1,-sse4.2,-cx16,-lahf_lm,-popcnt,-avx,-avx2,-f16c,-fma,-bmi1,-bmi2,-xsave,-abm"
ENV = dict(os.environ, LC_ALL="C")
ENV.pop("LD_PRELOAD", None)
ENV.pop("LD_LIBRARY_PATH", None)


def output(argv):
    result = subprocess.run(argv, env=ENV, capture_output=True, text=True, timeout=20)
    if result.returncode or len(result.stdout) + len(result.stderr) > 262144:
        raise ValueError("inspection failed: " + str(argv[0]))
    return result.stdout


@dataclass(frozen=True)
class Artifact:
    path: Path
    dso: bool
    needed: Optional[str]
    used: Optional[str]
    dynamic: str


def inspect(path):
    path = Path(path).resolve(strict=True)
    with path.open("rb") as source:
        header = source.read(64)
    if len(header) != 64 or header[:6] != b"\x7fELF\x02\x01":
        raise ValueError("not a little-endian ELF64 artifact: " + str(path))
    kind, machine = struct.unpack_from("<HH", header, 16)
    if machine != 62 or kind not in (2, 3):
        raise ValueError("unsupported ELF architecture/type: " + str(path))
    program = output(["readelf", "-lW", str(path)])
    notes = output(["readelf", "-n", str(path)])
    dynamic = output(["readelf", "-dW", str(path)])
    def property_value(name):
        match = re.search(r"x86 ISA " + name + r":\s*([^\n]+)", notes)
        return match.group(1).strip() if match else None
    return Artifact(path, kind == 3 and "INTERP" not in program,
                    property_value("needed"), property_value("used"), dynamic)


def needs_execution(artifact):
    if artifact.needed == BASELINE:
        return False
    if artifact.dso and artifact.needed is None and artifact.used == BASELINE:
        return True
    raise ValueError(f"ISA unqualified: {artifact.path}: needed={artifact.needed}, used={artifact.used}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emulator", help="opt-in qemu-x86_64 binary; no implicit download/runner")
    parser.add_argument("--sysroot", help="isolated baseline runtime root for that emulator")
    parser.add_argument("--cpu-probe", help="baseline-built tests/isa-cpu.c executable")
    parser.add_argument("--library-path", help="explicit isolated guest library directories")
    parser.add_argument("artifacts", nargs="+")
    args = parser.parse_args()
    artifacts = [inspect(p) for p in args.artifacts]
    pending = [a for a in artifacts if needs_execution(a)]
    executables = [a for a in artifacts if not a.dso]
    if args.emulator:
        if not all((args.sysroot, args.cpu_probe, args.library_path)) or not executables:
            raise ValueError("baseline execution requires sysroot, cpu probe, explicit library path and a consumer")
        root = Path(args.sysroot).resolve(strict=True)
        probe = inspect(args.cpu_probe)
        if probe.dso or needs_execution(probe):
            raise ValueError("CPU probe must be a baseline-declared executable")
        prefixes = args.library_path.split(":")
        if any(not p.startswith("/") or ".." in Path(p).parts for p in prefixes):
            raise ValueError("guest library paths must be explicit absolute directories")
        # The consumer must import the exact pending DSO, not another installed copy.
        for a in pending:
            match = re.search(r"\(SONAME\).*\[([^]]+)\]", a.dynamic)
            if not match or "/" in match.group(1):
                raise ValueError("pending DSO needs a simple SONAME")
            name = match.group(1)
            if not any(re.search(r"\(NEEDED\).*\[" + re.escape(name) + r"\]", e.dynamic) for e in executables):
                raise ValueError("no declared executable consumer for " + name)
            candidates = [root / prefix.lstrip("/") / name for prefix in prefixes]
            loaded = next((p for p in candidates if p.is_file()), None)
            if loaded is None or hashlib.sha256(loaded.read_bytes()).digest() != hashlib.sha256(a.path.read_bytes()).digest():
                raise ValueError("guest DSO does not match audited artifact: " + name)
            # Legacy DT_RPATH overrides LD_LIBRARY_PATH; reject that ambiguity.
            if any("(RPATH)" in e.dynamic for e in executables):
                raise ValueError("DT_RPATH consumer can override the audited guest DSO")
        emulator = str(Path(args.emulator).resolve(strict=True))
        def execute(path):
            argv = [emulator, "-cpu", CPU, "-L", str(root), "-E", "LD_LIBRARY_PATH=" + args.library_path, str(path)]
            result = subprocess.run(argv, env=ENV, capture_output=True, text=True, timeout=60)
            if result.returncode or len(result.stdout) + len(result.stderr) > 262144:
                raise ValueError("baseline execution failed: " + str(path) + ": exit=" + str(result.returncode))
            return result.stdout
        if execute(probe.path).strip() != "synapse.isa.cpu/v1: x86-64-baseline":
            raise ValueError("emulator CPU capability probe did not verify baseline")
        for executable in executables:
            execute(executable.path)
            print("baseline execution: " + str(executable.path) + ": PASS")
    elif pending:
        raise ValueError("undeclared DSO needs baseline consumer execution; ISA-used/flags alone do not qualify runtime")
    for a in artifacts:
        mode = "needed=baseline" if a.needed == BASELINE else "used=baseline + exact DSO/consumer baseline execution"
        print("ISA gate: " + str(a.path) + ": " + mode)
    # Bounded CPU compatibility evidence is not physical/network/release acceptance.
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print("ISA gate unqualified: " + str(error), file=sys.stderr)
        sys.exit(1)
