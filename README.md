<!-- SPDX-License-Identifier: MIT -->
# libsynapse-mdns

Independent Synapse-owned C17 mDNS library over Avahi. It supplies observations
and owned publication lifetime, never identity, trust, application readiness,
authoritative state or a second registry. It does not create a TCP listener.
Service types, instances and TXT are consumer data.

The initial implementation is extracted from qualified first-party Continuity
source without rewriting its history. Public **API/ABI major 1** uses
`<synapse/mdns.h>`, pkg-config `synapse-mdns` and SONAME
`libsynapse-mdns.so.1`. See [the API contract](docs/api.md).

Source development only: no packaging, system installation, deployment, tag,
accepted release or ISO inclusion is authorized.
Synapse AI Manager remains planning-only; this dependency does not implement it.

## Build and isolated SDK validation

Requires a C17 compiler, Avahi client/common development files, pkg-config,
Python 3 and standard ELF/binutils tools. No libsynapse-core, Wayland or GUI
headers are required. Console diagnostics are deterministic en_US; callers
own application JSON, identity, readiness and policy.

```sh
make all test                         # no real Avahi connection/announcement
make install DESTDIR="$PWD/.work/sdk" # isolated staging, never live installation
pkg-config --modversion synapse-mdns  # after supplying the staged SDK search path
```

`make test` exercises shared and static public API consumers, C++ linkage,
exact versioned exports, SONAME, installed SDK and the mock Avahi lifecycle.
An unavailable SDK or unsupported major must be explicit, not a source/provider
fallback. `make rig-build` builds an opt-in finite test consumer; it is never
run or installed by normal tests. `make wrong-major` produces a deliberately
faulty test library only, never an installed artifact.

`make isa-check` is a separate linked-artifact qualification gate. Compiler
baseline flags and ISA-used notes do not prove runtime requirements; rolling
host CRT/dependencies can still demand higher ISA. No unsupported architecture
or missing note is silently treated as target qualification. Physical network
rigs, target ISA, packaging and release acceptance remain distinct.

## Source lifecycle

Bootstrap is on `develop`; implementation lives on `feature/mdns-extraction`.
No `main` release history exists yet. See [AGENTS.md](AGENTS.md) and
[PROVENANCE.md](PROVENANCE.md). The library is independent of libsynapse-core;
network acquisition does not enter that libc-only support library.
