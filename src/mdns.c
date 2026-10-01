// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include <synapse/mdns.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/address.h>
#include <avahi-common/domain.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>

#define MDNS_POLL_MS 100U
#define MDNS_RESOLVERS_MAX 64U
#define MDNS_RETRY_MS 3000U

static void set_reason(char *out, size_t cap, const char *reason) {
    if (out && cap) snprintf(out, cap, "%s", reason);
}

static void avahi_reason(char *out, size_t cap, int error) {
    const char *text = avahi_strerror(error);
    if (out && cap) snprintf(out, cap, "avahi-err-%d-%.40s", error < 0 ? -error : error,
                            text ? text : "unknown");
}

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

static unsigned remaining_step(uint64_t deadline) {
    uint64_t now = monotonic_ms();
    if (now >= deadline) return 0;
    uint64_t left = deadline - now;
    return (unsigned)(left < MDNS_POLL_MS ? left : MDNS_POLL_MS);
}

static unsigned char ascii_lower(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

bool synapse_mdns_txt_next(const synapse_mdns_record *record, size_t *offset,
                           const uint8_t **bytes, size_t *length) {
    if (!record || !offset || !bytes || !length || record->txt_bytes > SYNAPSE_MDNS_TXT_WIRE_MAX ||
        *offset >= record->txt_bytes) return false;
    size_t size = record->txt[*offset];
    if (size > record->txt_bytes - *offset - 1) return false;
    *bytes = record->txt + *offset + 1;
    *length = size;
    *offset += size + 1;
    return true;
}

bool synapse_mdns_txt_value(const synapse_mdns_record *record, const char *key,
                            const uint8_t **value, size_t *length) {
    if (!record || !key || !*key || !value || !length) return false;
    *value = NULL;
    *length = 0;
    size_t key_len = strlen(key), offset = 0, size;
    const uint8_t *entry;
    while (synapse_mdns_txt_next(record, &offset, &entry, &size)) {
        if (size < key_len || (size > key_len && entry[key_len] != '=')) continue;
        bool match = true;
        for (size_t k = 0; k < key_len; k++) {
            if (ascii_lower(entry[k]) != ascii_lower((unsigned char)key[k])) {
                match = false;
                break;
            }
        }
        if (!match) continue;
        if (size > key_len) {
            *value = entry + key_len + 1;
            *length = size - key_len - 1;
        }
        return true;
    }
    return false;
}

typedef struct browse_context browse_context;
typedef struct {
    browse_context *context;
    AvahiServiceResolver *resolver;
    bool used;
} resolver_slot;

struct browse_context {
    synapse_mdns_observer observer;
    void *userdata;
    synapse_mdns_browse_report *report;
    synapse_mdns_record *record;
    bool stopped;
    resolver_slot resolvers[MDNS_RESOLVERS_MAX];
};

static void resolved(AvahiServiceResolver *resolver, AvahiIfIndex interface,
                     AvahiProtocol protocol, AvahiResolverEvent event,
                     const char *name, const char *type, const char *domain,
                     const char *host, const AvahiAddress *address, uint16_t port,
                     AvahiStringList *txt, AvahiLookupResultFlags flags, void *userdata) {
    (void)protocol;
    (void)flags;
    resolver_slot *slot = userdata;
    browse_context *ctx = slot->context;
    if (!ctx->stopped && event == AVAHI_RESOLVER_FOUND) {
        synapse_mdns_record *record = ctx->record;
        memset(record, 0, sizeof(*record));
        snprintf(record->instance, sizeof(record->instance), "%s", name ? name : "");
        snprintf(record->type, sizeof(record->type), "%s", type ? type : "");
        snprintf(record->domain, sizeof(record->domain), "%s", domain ? domain : "");
        snprintf(record->host, sizeof(record->host), "%s", host ? host : "");
        record->interface_index = interface;
        record->port = port;
        if (address && (address->proto == AVAHI_PROTO_INET || address->proto == AVAHI_PROTO_INET6)) {
            avahi_address_snprint(record->address, sizeof(record->address), address);
            record->family = address->proto == AVAHI_PROTO_INET ? SYNAPSE_MDNS_IPV4 : SYNAPSE_MDNS_IPV6;
        }
        /* Keep the complete protocol-bounded TXT RDATA, not just the first N
         * keys: otherwise a consumer's identity key could silently disappear. */
        for (AvahiStringList *entry = txt; entry; entry = entry->next) {
            if (entry->size > SYNAPSE_MDNS_TXT_BYTES ||
                entry->size + 1 > sizeof(record->txt) - record->txt_bytes) {
                record->txt_truncated = true;
                continue;
            }
            record->txt[record->txt_bytes++] = (uint8_t)entry->size;
            memcpy(record->txt + record->txt_bytes, entry->text, entry->size);
            record->txt_bytes += entry->size;
            record->txt_count++;
        }
        ctx->report->limited |= record->txt_truncated;
        ctx->report->observations++;
        ctx->stopped = !ctx->observer(record, ctx->userdata);
    } else if (!ctx->stopped && event == AVAHI_RESOLVER_FAILURE) {
        ctx->report->resolver_failures++;
    }
    slot->resolver = NULL;
    slot->used = false;
    avahi_service_resolver_free(resolver);
}

static void service_found(AvahiServiceBrowser *browser, AvahiIfIndex interface,
                          AvahiProtocol protocol, AvahiBrowserEvent event,
                          const char *name, const char *type, const char *domain,
                          AvahiLookupResultFlags flags, void *userdata) {
    (void)flags;
    browse_context *ctx = userdata;
    if (ctx->stopped) return;
    if (event == AVAHI_BROWSER_FAILURE) {
        ctx->stopped = true;
        set_reason(ctx->report->reason, sizeof(ctx->report->reason), "browser-failed");
        return;
    }
    if (event != AVAHI_BROWSER_NEW) return;
    for (size_t i = 0; i < MDNS_RESOLVERS_MAX; i++) {
        resolver_slot *slot = &ctx->resolvers[i];
        if (slot->used) continue;
        slot->context = ctx;
        slot->used = true;
        slot->resolver = avahi_service_resolver_new(avahi_service_browser_get_client(browser),
                                                    interface, protocol, name, type, domain,
                                                    AVAHI_PROTO_UNSPEC, 0, resolved, slot);
        if (!slot->resolver) {
            slot->used = false;
            ctx->report->resolver_failures++;
        }
        return;
    }
    ctx->report->limited = true;
}

int synapse_mdns_browse(const char *type, const char *domain, unsigned window_ms,
                       synapse_mdns_observer observer, void *userdata,
                       synapse_mdns_browse_report *report) {
    if (!report) return -1;
    memset(report, 0, sizeof(*report));
    set_reason(report->reason, sizeof(report->reason), "invalid-request");
    if (!type || !avahi_is_valid_service_type_strict(type) ||
        (domain && !avahi_is_valid_domain_name(domain)) ||
        !observer || window_ms == 0 || window_ms > 60000U) return -1;
    AvahiSimplePoll *poll = avahi_simple_poll_new();
    if (!poll) { set_reason(report->reason, sizeof(report->reason), "no-poll"); return -1; }
    AvahiClient *client = avahi_client_new(avahi_simple_poll_get(poll), 0, NULL, NULL, NULL);
    if (!client) {
        set_reason(report->reason, sizeof(report->reason), "no-client");
        avahi_simple_poll_free(poll);
        return -1;
    }
    browse_context ctx = {.observer = observer, .userdata = userdata, .report = report};
    ctx.record = calloc(1, sizeof(*ctx.record));
    if (!ctx.record) {
        set_reason(report->reason, sizeof(report->reason), "no-memory");
        avahi_client_free(client);
        avahi_simple_poll_free(poll);
        return -1;
    }
    AvahiServiceBrowser *browser = avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
                                                             type, domain, 0, service_found, &ctx);
    if (!browser) {
        set_reason(report->reason, sizeof(report->reason), "no-browser");
        free(ctx.record);
        avahi_client_free(client);
        avahi_simple_poll_free(poll);
        return -1;
    }
    report->available = true;
    set_reason(report->reason, sizeof(report->reason), "observed");
    uint64_t deadline = monotonic_ms() + window_ms;
    unsigned step;
    while (!ctx.stopped && (step = remaining_step(deadline)) != 0) {
        if (avahi_simple_poll_iterate(poll, (int)step) < 0) {
            set_reason(report->reason, sizeof(report->reason), "poll-failed");
            break;
        }
    }
    /* Stop outstanding asynchronous resolutions before their stack context
     * expires. client_free is not used as an implicit cancellation policy. */
    ctx.stopped = true;
    for (size_t i = 0; i < MDNS_RESOLVERS_MAX; i++) {
        if (ctx.resolvers[i].resolver) avahi_service_resolver_free(ctx.resolvers[i].resolver);
    }
    avahi_service_browser_free(browser);
    free(ctx.record);
    avahi_client_free(client);
    avahi_simple_poll_free(poll);
    return strcmp(report->reason, "observed") == 0 ? 0 : -1;
}

struct synapse_mdns_publisher {
    AvahiSimplePoll *poll;
    AvahiClient *client;
    AvahiEntryGroup *group;
    synapse_mdns_publication publication;
    bool committed;
    bool client_dead;
    bool dirty;
    bool pending;
    bool recover;
    uint32_t retries;
    uint64_t retry_at;
    char reason[SYNAPSE_MDNS_REASON_CAP];
};

static void group_state(AvahiEntryGroup *group, AvahiEntryGroupState state, void *userdata) {
    (void)group;
    synapse_mdns_publisher *ctx = userdata;
    if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
        ctx->committed = true;
        ctx->pending = false;
        ctx->dirty = false;
        set_reason(ctx->reason, sizeof(ctx->reason), "advertised");
    } else if (state == AVAHI_ENTRY_GROUP_FAILURE || state == AVAHI_ENTRY_GROUP_COLLISION) {
        ctx->committed = false;
        ctx->pending = false;
        ctx->dirty = true;
        set_reason(ctx->reason, sizeof(ctx->reason),
                   state == AVAHI_ENTRY_GROUP_FAILURE ? "group-failed" : "entry-collision");
    } else if (state == AVAHI_ENTRY_GROUP_REGISTERING && ctx->committed) {
        ctx->committed = false;
        ctx->pending = true;
        set_reason(ctx->reason, sizeof(ctx->reason), "re-registering");
    } else if (state == AVAHI_ENTRY_GROUP_UNCOMMITED && ctx->committed) {
        ctx->committed = false;
        ctx->pending = false;
        ctx->dirty = true;
        set_reason(ctx->reason, sizeof(ctx->reason), "uncommitted");
    }
}

static void client_state(AvahiClient *client, AvahiClientState state, void *userdata) {
    synapse_mdns_publisher *ctx = userdata;
    if (state == AVAHI_CLIENT_S_COLLISION || state == AVAHI_CLIENT_S_REGISTERING) {
        /* Release/rebuild outside callbacks, never while Avahi is still
         * constructing or dispatching an entry group. */
        ctx->committed = false;
        ctx->pending = false;
        ctx->dirty = true;
        set_reason(ctx->reason, sizeof(ctx->reason),
                   state == AVAHI_CLIENT_S_COLLISION ? "name-collision" : "re-registering");
    } else if (state == AVAHI_CLIENT_FAILURE) {
        ctx->committed = false;
        ctx->pending = false;
        ctx->dirty = true;
        ctx->client_dead = true;
        avahi_reason(ctx->reason, sizeof(ctx->reason), avahi_client_errno(client));
    } else if (state == AVAHI_CLIENT_S_RUNNING && !ctx->group) {
        ctx->dirty = true;
    }
}

static void release_group(synapse_mdns_publisher *ctx) {
    AvahiEntryGroup *group = ctx->group;
    ctx->group = NULL;
    if (group) avahi_entry_group_free(group);
}

static int attempt(synapse_mdns_publisher *ctx) {
    ctx->retry_at = monotonic_ms() + MDNS_RETRY_MS;
    ctx->dirty = true;
    if (!ctx->client || ctx->client_dead) {
        release_group(ctx);
        AvahiClient *old = ctx->client;
        ctx->client = NULL;
        if (old) avahi_client_free(old);
        ctx->client_dead = false;
        int error = 0;
        ctx->client = avahi_client_new(avahi_simple_poll_get(ctx->poll), 0, client_state, ctx, &error);
        if (!ctx->client) {
            avahi_reason(ctx->reason, sizeof(ctx->reason), error);
            return -1;
        }
    }
    if (avahi_client_get_state(ctx->client) != AVAHI_CLIENT_S_RUNNING) return -1;
    release_group(ctx);
    ctx->group = avahi_entry_group_new(ctx->client, group_state, ctx);
    if (!ctx->group) { set_reason(ctx->reason, sizeof(ctx->reason), "no-group"); return -1; }
    AvahiStringList *txt = NULL;
    for (size_t i = ctx->publication.txt_count; i > 0; i--) {
        const synapse_mdns_txt *entry = &ctx->publication.txt[i - 1];
        AvahiStringList *next = avahi_string_list_add_arbitrary(txt, entry->bytes, entry->length);
        if (!next) {
            avahi_string_list_free(txt);
            release_group(ctx);
            set_reason(ctx->reason, sizeof(ctx->reason), "no-memory");
            return -1;
        }
        txt = next;
    }
    const synapse_mdns_publication *pub = &ctx->publication;
    int rc = avahi_entry_group_add_service_strlst(ctx->group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0,
                                                 pub->instance, pub->type,
                                                 pub->domain[0] ? pub->domain : NULL, NULL, pub->port, txt);
    avahi_string_list_free(txt);
    if (rc >= 0) {
        /* Establish the pre-commit state before dispatch: never overwrite a
         * state change delivered synchronously by the provider. */
        ctx->dirty = false;
        ctx->pending = true;
        set_reason(ctx->reason, sizeof(ctx->reason), ctx->retries ? "republishing" : "pending");
        rc = avahi_entry_group_commit(ctx->group);
    }
    if (rc < 0) {
        ctx->pending = false;
        ctx->dirty = true;
        avahi_reason(ctx->reason, sizeof(ctx->reason), avahi_client_errno(ctx->client));
        release_group(ctx);
        return -1;
    }
    /* Only the callback can declare establishment or invalidate this attempt. */
    return 0;
}

static bool valid_publication(const synapse_mdns_publication *pub) {
    if (!pub || !memchr(pub->instance, 0, sizeof(pub->instance)) ||
        !memchr(pub->type, 0, sizeof(pub->type)) || !memchr(pub->domain, 0, sizeof(pub->domain)) ||
        !avahi_is_valid_service_name(pub->instance) || !avahi_is_valid_service_type_strict(pub->type) ||
        (pub->domain[0] && !avahi_is_valid_domain_name(pub->domain)) ||
        pub->port == 0 || pub->txt_count > SYNAPSE_MDNS_TXT_MAX) return false;
    for (size_t i = 0; i < pub->txt_count; i++) {
        if (pub->txt[i].length > SYNAPSE_MDNS_TXT_BYTES) return false;
    }
    return true;
}

int synapse_mdns_publish_start(synapse_mdns_publisher **out,
                              const synapse_mdns_publication *publication,
                              unsigned wait_ms, bool recover,
                              char *reason, size_t reason_cap) {
    if (!out) return -1;
    if (*out) { set_reason(reason, reason_cap, "handle-active"); return -1; }
    set_reason(reason, reason_cap, "invalid-request");
    if (!valid_publication(publication) || wait_ms > 60000U) return -1;
    synapse_mdns_publisher *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) { set_reason(reason, reason_cap, "no-memory"); return -1; }
    ctx->publication = *publication;
    ctx->recover = recover;
    set_reason(ctx->reason, sizeof(ctx->reason), "pending");
    ctx->poll = avahi_simple_poll_new();
    if (!ctx->poll) {
        set_reason(reason, reason_cap, "no-poll");
        free(ctx);
        return -1;
    }
    ctx->client = avahi_client_new(avahi_simple_poll_get(ctx->poll), 0, client_state, ctx, NULL);
    if (!ctx->client) {
        set_reason(reason, reason_cap, "no-client");
        synapse_mdns_publish_stop(&ctx);
        return -1;
    }
    int rc = attempt(ctx);
    if (rc < 0 && !recover) {
        set_reason(reason, reason_cap, ctx->reason);
        synapse_mdns_publish_stop(&ctx);
        return -1;
    }
    uint64_t deadline = monotonic_ms() + wait_ms;
    unsigned step;
    while (ctx->pending && !ctx->committed && (step = remaining_step(deadline)) != 0) {
        if (avahi_simple_poll_iterate(ctx->poll, (int)step) < 0) {
            ctx->pending = false;
            ctx->dirty = true;
            set_reason(ctx->reason, sizeof(ctx->reason), "poll-failed");
        }
    }
    if (wait_ms && ctx->pending && !ctx->committed) set_reason(ctx->reason, sizeof(ctx->reason), "timeout");
    set_reason(reason, reason_cap, ctx->reason);
    *out = ctx;
    return 0;
}

int synapse_mdns_publish_iterate(synapse_mdns_publisher *ctx) {
    if (!ctx || !ctx->poll) return -1;
    int rc = avahi_simple_poll_iterate(ctx->poll, 0);
    if (rc < 0) {
        ctx->committed = false;
        ctx->pending = false;
        ctx->dirty = true;
        set_reason(ctx->reason, sizeof(ctx->reason), "poll-failed");
    }
    if (ctx->recover && !ctx->committed && ctx->dirty && monotonic_ms() >= ctx->retry_at) {
        if (ctx->retries < UINT32_MAX) ctx->retries++;
        (void)attempt(ctx);
    }
    return rc;
}

bool synapse_mdns_publish_state(const synapse_mdns_publisher *ctx,
                                bool *committed, uint32_t *retries,
                                char *reason, size_t reason_cap) {
    if (!ctx || !committed || !reason || !reason_cap) return false;
    *committed = ctx->committed;
    if (retries) *retries = ctx->retries;
    set_reason(reason, reason_cap, ctx->reason);
    return true;
}

void synapse_mdns_publish_stop(synapse_mdns_publisher **publisher) {
    if (!publisher || !*publisher) return;
    synapse_mdns_publisher *ctx = *publisher;
    *publisher = NULL;
    release_group(ctx);
    if (ctx->client) avahi_client_free(ctx->client);
    if (ctx->poll) avahi_simple_poll_free(ctx->poll);
    free(ctx);
}
