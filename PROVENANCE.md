<!-- SPDX-License-Identifier: MIT -->
# Source provenance and extraction authority

Owner authorization (2026-10-01): create `libsynapse-mdns/` directly under the
canonical workspace, initialize an independent MIT C17 Git Flow repository,
extract only the mDNS/Avahi library, no daemon/packaging/deployment.

## First-party origin

The initial implementation, public-header precursor and two fixtures originate
in the first-party `synapse-continuity` repository at commit `8780774b7aa8afb8304c0dbe7678fae272664589`.
The source is MIT with SPDX markers; its LICENSE notice is retained in this
repository's LICENSE. Git attributes these newly introduced paths to the owner,
Mario Giustiniani, and the owner authorized their extraction. This is continued
MIT first-party development, not migration of third-party material to MIT.
Qualified source history and run evidence in Continuity are not rewritten.

| Original path | Extracted path | Original blob SHA256 |
|---|---|---|
| `src/mdns.c` | `src/mdns.c` | `464be92ae0251ebf5d329cf8627390ffa78d486b6ec438bfe1a8b65a074f87f9` |
| `src/mdns.h` | `include/synapse/mdns.h` | `f1e91688ffc4f7e392ddd59d9332fcb3aaced47d323c97127d4b3c357ea628a8` |
| `tests/mdns-fixture.c` | `tests/mdns-fixture.c` | `e8ba4b5ca01580f12e1c8ccb6193946e8094ef509d65b2580e34ab021e2316f3` |
| `tests/mdns-pilot.c` | `tests/mdns-pilot.c` | `d4072e7bc6daf856acb8213e4f13a1f9869ff79898c247c5f47765e962ac478b` |

Extraction changes are inspectable in this repository's feature history:
installed include spelling, export visibility/C++ linkage, version metadata,
build/SDK tests and documentation. Browse/publication mechanics are retained.
New build files, version implementation and tests are first-party MIT work.
There is no bundled or copied Avahi implementation, core source or foreign
application source. No sources/artifacts from the excluded sibling workspace
are imported.

## Third-party boundary

Avahi client/common libraries and their existing system daemon remain upstream
dependencies with their own licenses and notices. API includes and independently
written test substitutes do not relabel those dependencies. The MIT notice is
for this repository's first-party source, not an aggregate runtime package or
image. No package is produced by this extraction authorization.
