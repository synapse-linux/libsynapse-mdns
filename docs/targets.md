<!-- SPDX-License-Identifier: MIT -->
# CPU targets and DGX Spark qualification

The required target matrix includes x86-64/Strix Halo and NVIDIA DGX Spark
(GB10). Library discovery is CPU-only: no CUDA, GPU driver API, engine runtime,
model, GPU admission lease or new background service is required. Preserve
existing Avahi ownership and unrelated running applications.

## Architecture and API boundary

DGX Spark is Linux AArch64 little-endian LP64, not an x86-64 CPU. Build its own
shared/static SDK and callers using the target compiler. Public API/ABI major 1,
SONAME libsynapse-mdns.so.1, eight versioned exports and pkg-config module/version
remain unchanged. This is source/API compatibility with an architecture-native
C ABI, not binary interchange or serialized structure compatibility with x86-64.

Make selects the **compiler target**, not the build host. Owned AArch64 flags
are `-march=armv8-a -mtune=generic`; x86-64 flags remain unchanged. No native-CPU
flag or GPU architecture is selected. Source flags are not proof that compiler,
CRT and dependency bytes run on every Armv8-A processor. The currently scoped
ARM target is DGX Spark; general AArch64-baseline support is not claimed.

## Independent SDK and development prerequisites

Requires target C17 GCC/Clang, Avahi client/common development files (the 0.8
Ubuntu API as well as newer qualified Avahi), pkg-config, Python and GNU ELF
tools. No Continuity, core or session/GUI dependency is added. An existing Avahi
daemon alone does not supply development headers or unversioned linker names.

Missing development files may be staged in an owned temporary SDK from matching
official upstream binary packages, without live installation. Record package
versions/digests and runtime selection; retain notices. Do not silently use
sibling-project source or copy private library implementation into a consumer.
Library installation still requires a nonempty isolated DESTDIR.

```sh
make -rR all test CC=gcc BUILD_DIR=/owned/native-gcc
make install CC=gcc BUILD_DIR=/owned/native-gcc DESTDIR=/owned/native-sdk
make dgx-check CC=gcc BUILD_DIR=/owned/native-gcc
```

Run strict Clang, shared/static/C++ external SDK callers, ABI/export/dependency
checks, mock lifecycle, GCC ASan/UBSan/leak checking, GCC analyzer, genuine
scan-build, independent reproducibility and hardening. Ordinary test includes
compiler-target flag and wrong-platform/ELF/producer refusal regressions. It
never acquires real Avahi, executes an emulator or claims physical acceptance.

## Explicit native DGX artifact gate

`make dgx-check` explicitly checks native Linux AArch64 little-endian LP64 and
the observed NVIDIA_DGX_Spark platform, linked AArch64 ELF64 producer/callers,
RELRO/NOW/NX, SDK SONAME, correct shared import and static/mock boundaries.
Controlled runtime selection and a hash-matched SONAME alias bind callers to
the audited producer. It executes shared/static public callers and the
network-free mechanical fixture with bounded time and diagnostics. Record
artifact/runtime hashes, target/toolchain/CPU inventory and process exits.

The platform check is test inventory, never network identity/authentication.
This is actual native target/path execution, **not a general Armv8-A ISA proof**,
exhaustive instruction/dependency audit or a physical mDNS result. `isa-check`
remains x86-64-only; its higher-ISA failures remain untouched. GPU features,
engine readiness and model output are outside this library's qualification.

## Physical and release gates

Use separately authorized, finite, opt-in library pilots on DGX Spark and an
x86-64 peer. Verify generic foreign service/TXT observations both ways using
each architecture's independently built shared SDK. Qualify owned-name recovery
separately where needed. Ports in announcements are caller metadata; they do
not prove a listener, per-interface reachability, trust or application readiness.

Only owned processes/staging may be retired. Keep unrelated services/listeners,
models, provider configuration and firewall unchanged. Preserve failed attempts,
source/producer/runtime identities, receipts and cleanup evidence. Physical
DGX mDNS acceptance is mandatory before closing the expanded release target
matrix; older x86-only acceptance is retained as bounded historical evidence.
Source integration, accepted release, packaging/publication and live deployment
remain separate authorized transitions. Supporting this library on Spark does
not authorize an ARM image or claim that Continuity/other consumers are ported.
