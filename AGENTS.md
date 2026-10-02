<!-- SPDX-License-Identifier: MIT -->
# libsynapse-mdns source contract

Inherit the canonical Synapse workspace policy. This is an independent MIT C17
library, authorized by the owner on 2026-10-01, not a daemon, application,
authoritative registry or image-inclusion decision.

- Own only policy-free mDNS/Avahi browsing, TXT representation, publication,
  bounded diagnostics and resource lifetime. Identity, trust, self filtering,
  deduplication, application readiness and service/TXT policy remain consumers'.
- Do not add Continuity, libsynapse-core, systemd-session, Wayland, Qt, engine or
  application-state dependencies. Avahi uses its existing system service.
- Do not add protocols or restart/reconfigure host services implicitly. Never
  put credentials or application payloads in test announcements or diagnostics.
- Public API/ABI is versioned; layout, symbol or semantic incompatibility needs
  a new major. No silent fallback for unavailable/unsupported dependencies.
- Preserve source provenance and the inherited Continuity MIT copyright notice.
  Avahi and other third-party dependencies retain their own licenses.
- Git Flow: bootstrap documentation on develop; feature/* starts from develop.
  main has no history until an accepted release. Never merge, push, tag, package
  or deploy without corresponding authorization. Do not copy source into a
  consumer as a substitute for the versioned library dependency.
- Normal tests must never connect to a real Avahi service or publish mDNS. Real
  network rigs are opt-in, finite and clean up only owned processes/artifacts.
- Validate strict GCC/Clang, public shared/static SDK consumption, symbols/ABI,
  staged installation, dependency refusals, ASan/UBSan, analyzers, reproducibility,
  hardening and per-artifact target ISA before a release/packaging claim.
- Required CPU targets include x86-64/Strix Halo and DGX Spark (GB10, Linux
  AArch64 little-endian LP64). Build separate binaries; preserve the public API1
  boundary. x86-64 ISA gates never qualify ARM64. The explicit native dgx-check
  is target/path evidence, not an all-Arm baseline or physical-network proof.
  Do not acquire GPU/engine authority or disturb existing services to qualify
  this CPU-only library. See docs/targets.md.
- make install must be exercised with nonempty DESTDIR, not on the live host.
  Source, staging, packaging and live deployment are separate transitions.
