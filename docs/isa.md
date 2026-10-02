<!-- SPDX-License-Identifier: MIT -->
# x86-64 linked-artifact qualification

CPU requirements belong to the produced artifact and its runtime, not the build
host's flags. A rolling optimized host is not the baseline target SDK. Retain
negative receipts rather than overwriting them with a build/suite success.

## ELF properties are different evidence

- `GNU_PROPERTY_X86_ISA_1_NEEDED` is the declared minimum required ISA. A PIE or
  executable in this gate must declare exactly `x86-64-baseline`; absent, v2/v3/v4,
  unsupported architecture and unlinked object inputs are not qualified.
- `GNU_PROPERTY_X86_ISA_1_USED` describes instructions observed in code. It is
  not a required-ISA declaration. A library with dispatched implementations may
  legitimately use instructions beyond its required baseline.
- An ELF DSO may have **no NEEDED property** despite a baseline USED property.
  Absence is not evidence of an elevated requirement, but it is also not proof
  of a baseline runtime. The initial library gate incorrectly treated DSO and
  PIE note expectations as identical. Do not "fix" it by stamping a synthetic
  needed property, hiding a v2+ note, or treating source flags as qualification.

The type-aware gate distinguishes a DSO from a PIE using ELF type and interpreter.
A baseline-needed artifact passes the note check. An undeclared DSO is only
eligible for supplemental execution if its USED property is exactly baseline.
Without explicit execution evidence it still refuses qualification. Needed v2+
remains a refusal regardless of flags, used notes or a successful smoke test.

## Explicit baseline execution

Build library/shared/static consumers, mechanical fixtures and CPU probe in a
pinned baseline SDK first. `make isa-check` builds the test-only CPU probe;
no probe/emulator is installed or run by ordinary `make test`.

```sh
make isa-check BUILD_DIR=/path/to/neutral-build \
  ISA_EMULATOR=/usr/bin/qemu-x86_64 \
  ISA_SYSROOT=/path/to/isolated-runtime-root \
  ISA_LIBRARY_PATH=/guest/sdk/usr/lib:/guest/core/usr/lib
```

Use the same neutral compiler/SDK for any missing prerequisite. The explicit
emulator runs a fixed CPU model with post-baseline features disabled. A
baseline-built CPUID probe must verify actual capabilities before consumers run.
The default qemu64 model alone is insufficient because it exposes SSE3 and other
post-baseline features. The script runs shared and static public consumers and
mechanical fixtures, with bounded duration and inspected baseline-needed notes.

For every pending DSO, the consumer must import its SONAME and the selected guest
library file must hash-match the audited producer. Legacy DT_RPATH consumers are
refused because they could override the explicit library path. Host preload and
library environment are cleared. The isolated runtime closure must come from the
same recorded neutral SDK; do not mix it with the rolling host's runtime. The
emulator executable is build/test tooling, not a new product dependency.

## Meaning and limits

The combined note/CPU/consumer evidence qualifies the **specific byte artifacts**
for the exercised baseline paths. It is not an exhaustive instruction/path proof,
a full target image dependency audit, physical mDNS rendering/reachability,
hardware acceptance, packaging, release approval or deployment. Preserve the
runtime closure, producer/consumer hashes, SDK digest/package versions, CPU probe,
actual commands/exits, diagnostics and tests alongside the result.

Network-free regression tests cover absence/misclassification, PIE versus DSO,
needed versus used, higher requirements and missing supplemental inputs. These
mock tests are decision/parser evidence, never emulator execution receipts.
