<!-- SPDX-License-Identifier: MIT -->
# Public API/ABI 1

Candidate source version: `0.1.0-alpha.1`. Installed header:
`<synapse/mdns.h>`. pkg-config module: `synapse-mdns`. Dynamic SONAME:
`libsynapse-mdns.so.1`; eight public symbols under `SYNAPSE_MDNS_1.0`.
A static archive is also built. This alpha feature is not an accepted release
or package; a staged SDK is a validation artifact, not a deployed dependency.

## Compatibility

Compile against `SYNAPSE_MDNS_API_MAJOR == 1U`; validate the side-effect-free
`synapse_mdns_api_major()` before network work. The runtime reports major 1.
A consumer supports only the major it implements; unavailable or unsupported
libraries must be explicit refusals, never a copied implementation, alternative
provider or parsing of human output. ABI-layout, symbol or semantic breaks need
a new major, SONAME and symbol namespace. Compatible additions may use a new
minor without changing existing layouts/behavior. Native C ABI is per target;
do not pack/redefine structures or use ABI-changing compiler flags. C++ linkage
is provided, without a C++ runtime dependency in this library.

Public symbols: `synapse_mdns_api_major`, `synapse_mdns_browse`,
`synapse_mdns_txt_next`, `synapse_mdns_txt_value`,
`synapse_mdns_publish_start`, `synapse_mdns_publish_iterate`,
`synapse_mdns_publish_state`, `synapse_mdns_publish_stop`.
No Avahi types or pointers appear in the public header. Dynamic consumers need
no Avahi compile flags; static pkg-config linking includes its private libraries.
Runtime Avahi dependencies retain their upstream licenses and system-service
ownership. The library has no core, Wayland, GUI or application-state dependency.

## Observations

`browse(type, domain, window_ms, observer, userdata, report)` validates a DNS-SD
service type and optional domain (NULL means Avahi's default). A valid window is
1..60000 ms, monotonic after browser creation. Avahi client creation is outside
that poll window. The observer receives a borrowed, heap-backed record:

- instance/type/domain/host buffers of 256 bytes;
- address buffer of 48 bytes, port, interface index, family (0/4/6);
- complete raw TXT RDATA up to its 65535-byte DNS RR bound, entry count and
  truncation flag. An entry has at most 255 bytes.

Records and returned byte spans expire on callback return. Copy required data,
not pointers; avoid placing the approximately 66 KiB record on a small stack.
No observation-count cap, deduplication, self filter, IPv4/interface preference,
identity, trust, caching or persistence exists here. IPv6 may resolve without
IPv4; interface identity is supplied but dialability of link-local addresses is
not claimed. Hosts/addresses and TXT are untrusted reachability/metadata.

The observer returning false ends the pass, including suppression of later
queued observations. All pending resolvers are explicitly cancelled before
context destruction. At most 64 are pending concurrently; admission overflow or
oversized TXT sets `report.limited`. Individual resolution failures are counted.
`available` means a client/browser could be created, not a complete inventory.
`observations` counts delivered callbacks including the final callback. Successful
emptiness is distinct from unavailable backend/browser/poll failure: return 0
versus -1 and stage reason. `limited`/`resolver_failures` remain explicit even on
successful completion. Callers decide whether a partial result is useful.

TXT iteration uses a zero-initialized byte offset. Length-prefixed entries keep
unknown keys, flags, empty values, binary/NUL data and original list order. Lookup
is optional ASCII-case-insensitive DNS-SD key matching; first duplicate wins.
Missing key (false), key-only flag (true/NULL/zero), empty value (true/non-NULL/zero)
and binary value are distinct. End/malformed iteration returns false; do not
interpret prior output spans after a failed call.

## Publication and ownership

The caller supplies a valid instance/type/optional domain, nonzero port and up to
16 opaque TXT entries of at most 255 bytes each. This is publication metadata,
not a socket listener, credential/payload channel or authority to connect.
There is no automatic discovery or publication before explicit API calls.

`publish_start(&handle, publication, wait_ms, recover, reason, cap)` requires a
NULL handle, validates inputs and copies metadata. Startup wait is 0..60000 ms;
zero returns pending without waiting. An active handle cannot be overwritten.
An allocation/client/group setup failure with recovery disabled returns -1 with
no handle. Recovery-enabled group failure retains client/poll and returns an owned
handle with `committed:false`; successful start does not mean establishment.
A one-attempt probe can likewise return a noncommitted handle after timeout or
callback failure; inspect live state and stop it. No input pointer is retained.

Only an Avahi establishment callback sets committed. It is acknowledgement of
an entry group, not per-interface availability, remote receipt, authenticated
origin or functional application readiness. Invalidating client/group state or
poll failure clears success. Collided groups/dead clients are replaced outside
callback dispatch; synchronous callbacks are not overwritten by constructor
returns. Iterate polls without a blocking poll wait. Avahi reconnection still
uses its client API. Recovery attempts, including after the initial attempt,
are at least three monotonic seconds apart; the counter counts attempts, not
poll passes. No host Avahi service/configuration restart/change is performed.

A handle is single-owner, sequential and non-reentrant; do not concurrently
iterate/stop it or stop during its callbacks. Stop accepts NULL, is idempotent,
clears the caller's handle and frees the authoritative current resources.

## Diagnostics and language

Return codes and report fields are machine semantics. Bounded reason strings
are en_US stage labels, not executable input; retain unknown labels diagnostically
rather than treating them as authority. Initial stages include `invalid-request`,
`handle-active`, `no-memory`, `no-poll`, `no-client`, `no-browser`, `no-group`,
`browser-failed`, `poll-failed`, `pending`, `timeout`, `observed`, `advertised`,
`name-collision`, `entry-collision`, `group-failed`, `uncommitted`,
`re-registering`, `republishing` and bounded `avahi-err-<code>-<text>`.
No payload/TXT values are logged by the implementation. Caller-owned policies,
identifiers, provenance, versioned feature JSON and confirmation state remain
outside this API.
