# SPDX-License-Identifier: MIT
"""Consume an isolated installed SDK via pkg-config; never use private source."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

build = Path(sys.argv[1])
compiler = shlex.split(os.environ.get("CC", "cc"))
cflags = shlex.split(os.environ.get("TEST_CFLAGS", ""))
ldflags = shlex.split(os.environ.get("TEST_LDFLAGS", ""))
with tempfile.TemporaryDirectory(prefix="synapse-mdns-sdk-") as temporary:
    root = Path(temporary)
    stage = root / "stage"
    subprocess.run(["make", "--no-print-directory", "install", "BUILD_DIR=" + str(build),
                    "DESTDIR=" + str(stage), "PREFIX=/usr"], check=True)
    env = dict(os.environ, PKG_CONFIG_SYSROOT_DIR=str(stage),
               PKG_CONFIG_LIBDIR=str(stage / "usr/lib/pkgconfig"), PKG_CONFIG_PATH="")
    pc = shlex.split(os.environ.get("PKG_CONFIG", "pkg-config"))
    subprocess.run(pc + ["--exists", "synapse-mdns >= 0.1.0-alpha.1"], env=env, check=True)
    includes = shlex.split(subprocess.check_output(pc + ["--cflags", "synapse-mdns"], env=env, text=True))
    libs = shlex.split(subprocess.check_output(pc + ["--libs", "synapse-mdns"], env=env, text=True))
    static_libs = shlex.split(subprocess.check_output(pc + ["--static", "--libs", "synapse-mdns"], env=env, text=True))
    assert "-lavahi-client" in static_libs and "-lavahi-common" in static_libs, static_libs
    assert all("avahi" not in flag for flag in includes + libs), (includes, libs)
    source = root / "consumer.c"
    source.write_text('#include <synapse/mdns.h>\n'
                      '_Static_assert(SYNAPSE_MDNS_API_MAJOR == 1U, "unsupported header");\n'
                      'int main(void) { return synapse_mdns_api_major() != SYNAPSE_MDNS_API_MAJOR; }\n')
    runtime = dict(os.environ, LD_LIBRARY_PATH=str(stage / "usr/lib"))
    for static in (False, True):
        target = root / ("consumer-static" if static else "consumer")
        selected = ([str(stage / "usr/lib/libsynapse-mdns.a") if flag == "-lsynapse-mdns" else flag
                     for flag in static_libs] if static else libs)
        subprocess.run(compiler + cflags + includes + [str(source), "-o", str(target)] + selected + ldflags,
                       cwd=root, check=True)
        subprocess.run([str(target)], env=runtime, check=True)
    cpp = root / "consumer.cpp"
    cpp.write_text('#include <synapse/mdns.h>\n'
                   'static_assert(SYNAPSE_MDNS_API_MAJOR == 1U, "unsupported header");\n'
                   'int main() { return synapse_mdns_api_major() != SYNAPSE_MDNS_API_MAJOR; }\n')
    # This C++ program needs no C++ runtime; the same compiler driver checks
    # language/linkage with no independent sanitizer runtime substitution.
    cppflags = [flag for flag in cflags if not flag.startswith("-std=")] + ["-std=c++17"]
    subprocess.run(compiler + cppflags + includes + [str(cpp), "-o", str(root / "consumer-cpp")] + libs + ldflags,
                   cwd=root, check=True)
    subprocess.run([str(root / "consumer-cpp")], env=runtime, check=True)
    header = stage / "usr/include/synapse/mdns.h"
    assert header.exists() and not list(stage.rglob("*.c")), "SDK must not install implementation/test source"
    assert (stage / "usr/lib/libsynapse-mdns.so").resolve().name == "libsynapse-mdns.so.1.0.0"
print("isolated SDK: pkg-config, external C shared/static and C++ consumers, no private source: OK")
