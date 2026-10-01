# SPDX-License-Identifier: MIT
"""Generate standard pkg-config metadata into the selected build directory."""
from pathlib import Path
import sys

source, target, prefix, libdir, includedir, version = sys.argv[1:]
text = Path(source).read_text()
for marker, value in zip(
    ("@PREFIX@", "@LIBDIR@", "@INCLUDEDIR@", "@VERSION@"),
    (prefix, libdir, includedir, version),
):
    text = text.replace(marker, value)
Path(target).write_text(text)
