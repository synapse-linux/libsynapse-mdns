<!-- SPDX-License-Identifier: MIT -->
# Changelog

This changelog records first-party library source/API changes. It does not imply
package publication, image inclusion, remote authentication or deployment.

## 0.1.0-alpha.1 — release candidate (2026-10-18)

- Establish the independent C17 shared/static mDNS SDK with public API/ABI major
  1, SONAME `libsynapse-mdns.so.1`, pkg-config module `synapse-mdns`, and eight
  versioned public symbols.
- Preserve bounded generic DNS-SD observation, complete raw TXT access, explicit
  publication, opaque single-owner lifetime, Avahi collision/recovery and
  fail-closed diagnostics. Application metadata, identity, trust, readiness,
  endpoints and policy remain owned by consumers.
- Qualify the existing Continuity consumer boundary and independent public SDK
  on x86-64/Strix Halo and DGX Spark/GB10 Linux AArch64, including bounded
  bidirectional physical mDNS receipts and exact architecture-specific evidence.
- Retain MIT first-party source and provenance; Avahi/runtime dependencies retain
  upstream license and service ownership.

This remains a **release candidate** until the signed immutable source identity,
packages and controlled installation/verification gates have completed. The
version number is immutable once the signed release tag is created; any source
change after that requires a new version. Historical negative/failed receipts
remain distinct from final passing receipts.
