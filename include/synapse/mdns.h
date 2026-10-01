// SPDX-License-Identifier: MIT
#ifndef SYNAPSE_MDNS_H
#define SYNAPSE_MDNS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* libsynapse-mdns API/ABI major 1. Discovery supplies observations, never
 * identity, trust or application policy. No Continuity, core, session-bus or
 * presentation dependency. Layout/semantic incompatibility requires a major
 * change; this alpha candidate is not a packaging/release acceptance claim. */
#define SYNAPSE_MDNS_API_MAJOR 1U
#if defined(__GNUC__) || defined(__clang__)
#define SYNAPSE_MDNS_API __attribute__((visibility("default")))
#else
#define SYNAPSE_MDNS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Side-effect-free runtime contract check; no Avahi client is created. */
SYNAPSE_MDNS_API uint32_t synapse_mdns_api_major(void);
#define SYNAPSE_MDNS_NAME_CAP 256U
#define SYNAPSE_MDNS_ADDR_CAP 48U
#define SYNAPSE_MDNS_TXT_MAX 16U /* publication entries, not a discovery limit */
#define SYNAPSE_MDNS_TXT_BYTES 255U
#define SYNAPSE_MDNS_TXT_WIRE_MAX 65535U /* DNS RR RDLENGTH bound */
#define SYNAPSE_MDNS_REASON_CAP 64U

typedef struct {
    size_t length;
    /* DNS-SD TXT entries are opaque bytes, not necessarily key=value or UTF-8. */
    uint8_t bytes[SYNAPSE_MDNS_TXT_BYTES];
} synapse_mdns_txt;

typedef enum {
    SYNAPSE_MDNS_UNSPEC = 0,
    SYNAPSE_MDNS_IPV4 = 4,
    SYNAPSE_MDNS_IPV6 = 6
} synapse_mdns_family;

typedef struct {
    char instance[SYNAPSE_MDNS_NAME_CAP];
    char type[SYNAPSE_MDNS_NAME_CAP];
    char domain[SYNAPSE_MDNS_NAME_CAP];
    char host[SYNAPSE_MDNS_NAME_CAP];
    char address[SYNAPSE_MDNS_ADDR_CAP];
    uint16_t port;
    int interface_index;
    synapse_mdns_family family;
    size_t txt_count;
    size_t txt_bytes;
    /* Complete DNS TXT RDATA: one length octet then bytes for each entry.
     * Kept in a heap-backed borrowed record, not on a consumer's stack. */
    uint8_t txt[SYNAPSE_MDNS_TXT_WIRE_MAX];
    bool txt_truncated;
} synapse_mdns_record;

/* Borrowed record valid only during the callback. false ends the bounded pass.
 * No self filtering, deduplication, IPv4 preference or caching occurs here. */
typedef bool (*synapse_mdns_observer)(const synapse_mdns_record *record, void *userdata);

typedef struct {
    bool available;
    bool limited; /* a TXT/pending-resolver bound was reached */
    size_t observations;
    size_t resolver_failures;
    char reason[SYNAPSE_MDNS_REASON_CAP];
} synapse_mdns_browse_report;

/* Browse a supplied service type/domain (NULL domain means Avahi's default).
 * Window is 1..60000 ms. Unavailable backend is an explicit failure, not empty
 * success. Outstanding resolvers are cancelled before returning. */
SYNAPSE_MDNS_API int synapse_mdns_browse(const char *type, const char *domain, unsigned window_ms,
                       synapse_mdns_observer observer, void *userdata,
                       synapse_mdns_browse_report *report);

/* Iterate raw entries using a zero-initialized byte offset. Returned spans are
 * borrowed; false means end or malformed encoding. Empty entries are valid. */
SYNAPSE_MDNS_API bool synapse_mdns_txt_next(const synapse_mdns_record *record, size_t *offset,
                           const uint8_t **bytes, size_t *length);

/* Optional DNS-SD key lookup: ASCII case-insensitive; returns a borrowed value
 * and its byte length. A missing key, key-only flag and an empty value remain
 * distinct; embedded NUL bytes are preserved. first matching entry wins. */
SYNAPSE_MDNS_API bool synapse_mdns_txt_value(const synapse_mdns_record *record, const char *key,
                            const uint8_t **value, size_t *length);

typedef struct {
    char instance[SYNAPSE_MDNS_NAME_CAP];
    char type[SYNAPSE_MDNS_NAME_CAP];
    char domain[SYNAPSE_MDNS_NAME_CAP]; /* empty means Avahi's default */
    uint16_t port;
    size_t txt_count;
    synapse_mdns_txt txt[SYNAPSE_MDNS_TXT_MAX];
} synapse_mdns_publication;

typedef struct synapse_mdns_publisher synapse_mdns_publisher;

/* Copies the publication. *out must be NULL; an existing handle is never
 * overwritten. Caller owns the returned handle. recover=true keeps
 * a failed group alive for recovery; recover=false is a one-attempt probe.
 * Startup wait is 0..60000 ms. committed means Avahi acknowledged the group,
 * not that every network interface answered or a remote peer connected. */
SYNAPSE_MDNS_API int synapse_mdns_publish_start(synapse_mdns_publisher **out,
                              const synapse_mdns_publication *publication,
                              unsigned wait_ms, bool recover,
                              char *reason, size_t reason_cap);
/* Nonblocking poll pass; recovery attempts are separated by at least 3 s.
 * Reconnection uses Avahi's client API, not an application-owned daemon. */
SYNAPSE_MDNS_API int synapse_mdns_publish_iterate(synapse_mdns_publisher *publisher);
SYNAPSE_MDNS_API bool synapse_mdns_publish_state(const synapse_mdns_publisher *publisher,
                                bool *committed, uint32_t *retries,
                                char *reason, size_t reason_cap);
/* Safe on NULL and on an already stopped handle; clears *publisher. */
SYNAPSE_MDNS_API void synapse_mdns_publish_stop(synapse_mdns_publisher **publisher);

#ifdef __cplusplus
}
#endif

#endif
