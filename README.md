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
This dependency does not implement an AI manager, engine, launcher or registry.

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

## Required target matrix

The owner requires both x86-64 (including Strix Halo) and **DGX Spark / GB10
Linux AArch64**. They use the same source, public API/ABI major and SONAME, but
separate architecture-specific binaries and native C ABIs. An x86-64 binary or
its baseline receipt does not qualify ARM64. No CUDA/GPU runtime is required.

Compiler-target flags are conservative: x86-64 uses `-march=x86-64`, AArch64
uses `-march=armv8-a`; neither selects `native`. See [DGX qualification](docs/targets.md)
for the explicit native artifact gate, missing-development-file staging and
physical cross-architecture mDNS acceptance. `make test` includes network-free
compiler-target/refusal regressions, never a hardware or LAN qualification.

`make isa-check` is a separate **x86-64-only** [type-aware linked-artifact ISA gate](docs/isa.md).
Compiler flags/ISA-used alone do not qualify runtime; rolling host CRT/dependencies
can demand higher ISA. A DSO lacking ISA-needed requires explicit isolated
baseline execution, a capability probe and a hash-matched consumer/producer;
missing executable notes and v2+ needs still fail closed. No emulator is downloaded
or run implicitly. CPU-path evidence, physical target/network acceptance,
packaging and release remain distinct.

## Source lifecycle

Bootstrap and qualified extraction are integrated on `develop`; feature history
is retained. DGX target work starts from develop on `feature/arm64-mdns`.
No `main` release history exists yet. See [AGENTS.md](AGENTS.md) and
[PROVENANCE.md](PROVENANCE.md). The library is independent of libsynapse-core;
network acquisition does not enter that libc-only support library.
