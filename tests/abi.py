# SPDX-License-Identifier: MIT
"""Inspect the production ELF surface, never create an Avahi client."""
import subprocess
import sys

library = sys.argv[1]
expected = {
    "synapse_mdns_api_major", "synapse_mdns_browse", "synapse_mdns_txt_next",
    "synapse_mdns_txt_value", "synapse_mdns_publish_start", "synapse_mdns_publish_iterate",
    "synapse_mdns_publish_state", "synapse_mdns_publish_stop",
}
symbols = subprocess.check_output(["nm", "-D", "--defined-only", library], text=True)
exports = {line.split()[-1].split("@")[0] for line in symbols.splitlines()
           if line.split()[-1] != "SYNAPSE_MDNS_1.0"}
assert exports == expected, (exports, expected)
assert all(name + "@@SYNAPSE_MDNS_1.0" in symbols for name in expected), symbols
dynamic = subprocess.check_output(["readelf", "-d", library], text=True)
assert "[libsynapse-mdns.so.1]" in dynamic, dynamic
assert "RPATH" not in dynamic and "RUNPATH" not in dynamic, dynamic
assert "libsynapse-core" not in dynamic and "libwayland" not in dynamic, dynamic
assert "libavahi-client.so" in dynamic and "libavahi-common.so" in dynamic, dynamic
print("ELF ABI 1: exact eight versioned exports, SONAME, direct dependencies, no runtime search path: OK")
