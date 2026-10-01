<!-- SPDX-License-Identifier: MIT -->
# libsynapse-mdns

Independent Synapse-owned C17 mDNS library over Avahi. It supplies observations
and owned publication lifetime, never identity, trust, application readiness,
authoritative state or a second registry. It does not create a TCP listener.
Service types, instances and TXT are consumer data.

The owner authorized this name, this direct-child repository and Git
initialization on 2026-10-01. Source development only: no packaging, system
installation, deployment, tag, accepted release or ISO inclusion is authorized.
Synapse AI Manager remains planning-only; this dependency does not implement it.

Bootstrap is on `develop`; implementation lives on `feature/mdns-extraction`.
No `main` release history exists yet. See [AGENTS.md](AGENTS.md) and
[PROVENANCE.md](PROVENANCE.md). The library is independent of libsynapse-core;
network acquisition does not enter that libc-only support library.
