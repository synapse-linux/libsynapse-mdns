// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
/* In-process fake Avahi client/poll. Never connects to D-Bus or publishes on a
 * network. Real avahi-common validates names and handles DNS TXT bytes. */
#include <synapse/mdns.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>
#include <avahi-common/address.h>
#include <avahi-common/error.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/strlst.h>

struct AvahiSimplePoll { AvahiPoll api; };
struct AvahiClient { AvahiClientCallback callback; void *data; AvahiClientState state; int error; };
struct AvahiEntryGroup { AvahiClient *client; AvahiEntryGroupCallback callback; void *data; bool pending; };
struct AvahiServiceBrowser { AvahiClient *client; AvahiServiceBrowserCallback callback; void *data; };
struct AvahiServiceResolver {
    AvahiServiceResolverCallback callback;
    void *data;
    AvahiIfIndex interface;
    AvahiProtocol protocol;
    struct AvahiServiceResolver *next;
};

static uint64_t now_ms = 10000;
static AvahiEntryGroup *group;
static AvahiServiceBrowser *browser;
static AvahiServiceResolver *resolvers;
static int alive_polls, alive_clients, alive_groups, alive_browsers, alive_resolvers;
static int fail_poll, fail_client, fail_group, fail_publish, fail_commit, fail_browser, fail_resolver;
static int browse_mode, phase, additions, sync_collision, fail_iterate;
static const char *requested_type;
static synapse_mdns_publication sent;

int synapse_mdns_test_clock(clockid_t id, struct timespec *out);
int synapse_mdns_test_clock(clockid_t id, struct timespec *out) {
    assert(id == CLOCK_MONOTONIC);
    out->tv_sec = (time_t)(now_ms / 1000);
    out->tv_nsec = (long)(now_ms % 1000) * 1000000L;
    return 0;
}

AvahiSimplePoll *avahi_simple_poll_new(void) {
    if (fail_poll) return NULL;
    alive_polls++;
    return calloc(1, sizeof(AvahiSimplePoll));
}
void avahi_simple_poll_free(AvahiSimplePoll *p) { assert(p); alive_polls--; free(p); }
const AvahiPoll *avahi_simple_poll_get(AvahiSimplePoll *p) { return &p->api; }

AvahiClient *avahi_client_new(const AvahiPoll *api, AvahiClientFlags flags,
                              AvahiClientCallback callback, void *data, int *error) {
    (void)api; (void)flags;
    if (fail_client) { if (error) *error = AVAHI_ERR_DISCONNECTED; return NULL; }
    AvahiClient *c = calloc(1, sizeof(*c));
    assert(c);
    c->callback = callback; c->data = data; c->state = AVAHI_CLIENT_S_RUNNING;
    alive_clients++;
    if (callback) callback(c, c->state, data); /* constructor callback is synchronous */
    return c;
}
void avahi_client_free(AvahiClient *c) {
    assert(c && alive_groups == 0 && alive_browsers == 0 && alive_resolvers == 0);
    alive_clients--; free(c);
}
AvahiClientState avahi_client_get_state(AvahiClient *c) { return c->state; }
int avahi_client_errno(AvahiClient *c) { return c->error; }

AvahiEntryGroup *avahi_entry_group_new(AvahiClient *c, AvahiEntryGroupCallback cb, void *data) {
    if (fail_group) return NULL;
    assert(!group);
    group = calloc(1, sizeof(*group)); assert(group);
    group->client = c; group->callback = cb; group->data = data;
    alive_groups++;
    cb(group, AVAHI_ENTRY_GROUP_UNCOMMITED, data);
    return group;
}
int avahi_entry_group_free(AvahiEntryGroup *g) {
    assert(g == group); alive_groups--; free(g); group = NULL; return 0;
}
int avahi_entry_group_add_service_strlst(AvahiEntryGroup *g, AvahiIfIndex interface,
                                        AvahiProtocol protocol, AvahiPublishFlags flags,
                                        const char *name, const char *type, const char *domain,
                                        const char *host, uint16_t port, AvahiStringList *txt) {
    (void)flags; (void)host;
    assert(g == group && interface == AVAHI_IF_UNSPEC && protocol == AVAHI_PROTO_UNSPEC);
    additions++;
    if (fail_publish) { g->client->error = AVAHI_ERR_COLLISION; return -1; }
    memset(&sent, 0, sizeof(sent));
    snprintf(sent.instance, sizeof(sent.instance), "%s", name);
    snprintf(sent.type, sizeof(sent.type), "%s", type);
    snprintf(sent.domain, sizeof(sent.domain), "%s", domain ? domain : "");
    sent.port = port;
    for (AvahiStringList *e = txt; e; e = e->next) {
        assert(sent.txt_count < SYNAPSE_MDNS_TXT_MAX && e->size <= SYNAPSE_MDNS_TXT_BYTES);
        sent.txt[sent.txt_count].length = e->size;
        memcpy(sent.txt[sent.txt_count++].bytes, e->text, e->size);
    }
    return 0;
}
int avahi_entry_group_commit(AvahiEntryGroup *g) {
    if (fail_commit) { g->client->error = AVAHI_ERR_COLLISION; return -1; }
    if (sync_collision) {
        g->callback(g, AVAHI_ENTRY_GROUP_COLLISION, g->data);
        return 0;
    }
    g->pending = true;
    g->callback(g, AVAHI_ENTRY_GROUP_REGISTERING, g->data);
    return 0;
}

AvahiServiceBrowser *avahi_service_browser_new(AvahiClient *c, AvahiIfIndex interface,
                                               AvahiProtocol protocol, const char *type,
                                               const char *domain, AvahiLookupFlags flags,
                                               AvahiServiceBrowserCallback cb, void *data) {
    (void)domain; (void)flags;
    assert(interface == AVAHI_IF_UNSPEC && protocol == AVAHI_PROTO_UNSPEC);
    if (fail_browser) return NULL;
    assert(!browser);
    requested_type = type;
    browser = calloc(1, sizeof(*browser)); assert(browser);
    browser->client = c; browser->callback = cb; browser->data = data;
    alive_browsers++; phase = 0;
    return browser;
}
int avahi_service_browser_free(AvahiServiceBrowser *b) {
    assert(b == browser); alive_browsers--; free(b); browser = NULL; return 0;
}
AvahiClient *avahi_service_browser_get_client(AvahiServiceBrowser *b) { return b->client; }

AvahiServiceResolver *avahi_service_resolver_new(AvahiClient *c, AvahiIfIndex interface,
                                                 AvahiProtocol protocol, const char *name,
                                                 const char *type, const char *domain,
                                                 AvahiProtocol aprotocol, AvahiLookupFlags flags,
                                                 AvahiServiceResolverCallback cb, void *data) {
    (void)c; (void)name; (void)domain; (void)flags;
    assert(strcmp(type, requested_type) == 0 && aprotocol == AVAHI_PROTO_UNSPEC);
    if (fail_resolver) return NULL;
    AvahiServiceResolver *r = calloc(1, sizeof(*r)); assert(r);
    r->callback = cb; r->data = data; r->interface = interface; r->protocol = protocol;
    r->next = resolvers; resolvers = r; alive_resolvers++;
    return r;
}
int avahi_service_resolver_free(AvahiServiceResolver *r) {
    AvahiServiceResolver **p = &resolvers;
    while (*p && *p != r) p = &(*p)->next;
    assert(*p == r);
    *p = r->next; free(r); alive_resolvers--; return 0;
}

int avahi_simple_poll_iterate(AvahiSimplePoll *p, int timeout) {
    (void)p;
    if (fail_iterate) return -1;
    if (timeout > 0) now_ms += (unsigned)timeout;
    if (group && group->pending) {
        group->pending = false;
        group->callback(group, AVAHI_ENTRY_GROUP_ESTABLISHED, group->data);
    }
    if (!browser) return 0;
    if (phase++ == 0) {
        if (browse_mode == 3) {
            browser->callback(browser, 2, AVAHI_PROTO_INET, AVAHI_BROWSER_FAILURE,
                              "", requested_type, "local", 0, browser->data);
            return 0;
        }
        int count = browse_mode == 2 ? 70 : 3;
        for (int i = 0; i < count; i++) {
            browser->callback(browser, i + 2, i == 0 ? AVAHI_PROTO_INET6 : AVAHI_PROTO_INET,
                              AVAHI_BROWSER_NEW, "Inference node", requested_type, "local", 0, browser->data);
        }
    } else if (browse_mode != 2) {
        while (resolvers) {
            AvahiServiceResolver *r = resolvers;
            AvahiAddress address;
            assert(avahi_address_parse(r->protocol == AVAHI_PROTO_INET ? "192.0.2.4" : "2001:db8::4",
                                        r->protocol, &address));
            const uint8_t blob[] = {'b','l','o','b','=',0,'a'};
            AvahiStringList *txt = NULL;
            if (browse_mode == 6) {
                uint8_t large[255]; memset(large, 'x', sizeof(large));
                for (int i = 0; i < 300; i++) txt = avahi_string_list_add_arbitrary(txt, large, sizeof(large));
            }
            for (int i = 0; i < (browse_mode == 4 ? 20 : 1); i++)
                txt = avahi_string_list_add_arbitrary(txt, (const uint8_t *)"unknown=ok", 10);
            txt = avahi_string_list_add_arbitrary(txt, blob, sizeof(blob));
            txt = avahi_string_list_add(txt, "empty=");
            txt = avahi_string_list_add(txt, "flag");
            txt = avahi_string_list_add(txt, "engine=fixture");
            r->callback(r, r->interface, r->protocol,
                        browse_mode == 5 ? AVAHI_RESOLVER_FAILURE : AVAHI_RESOLVER_FOUND,
                        "Inference node", requested_type, "local", "node.local", &address,
                        18080, txt, 0, r->data);
            avahi_string_list_free(txt);
        }
    }
    return 0;
}

static void clean(void) {
    assert(alive_clients == 0 && alive_polls == 0 && alive_groups == 0 && alive_resolvers == 0 && alive_browsers == 0);
    fail_poll = fail_client = fail_group = fail_publish = fail_commit = fail_browser = fail_resolver = 0;
    sync_collision = fail_iterate = 0;
}
static void emit_group(AvahiEntryGroupState state) {
    assert(group && group->callback);
    group->callback(group, state, group->data);
}
static int observed;
static int stop_after = 3;
static bool observe(const synapse_mdns_record *r, void *data) {
    (void)data;
    assert(strcmp(r->type, "_fixture-launcher._tcp") == 0);
    assert(strcmp(r->host, "node.local") == 0 && strcmp(r->instance, "Inference node") == 0);
    assert(r->port == 18080 && r->interface_index >= 2);
    assert(r->family == (r->interface_index == 2 ? SYNAPSE_MDNS_IPV6 : SYNAPSE_MDNS_IPV4));
    assert(strcmp(r->address, r->interface_index == 2 ? "2001:db8::4" : "192.0.2.4") == 0);
    const uint8_t *value; size_t length;
    assert(synapse_mdns_txt_value(r, "ENGINE", &value, &length) && length == 7 && memcmp(value, "fixture", 7) == 0);
    assert(synapse_mdns_txt_value(r, "flag", &value, &length) && !value && !length);
    assert(synapse_mdns_txt_value(r, "empty", &value, &length) && value && !length);
    assert(synapse_mdns_txt_value(r, "blob", &value, &length) && length == 2 && value[0] == 0 && value[1] == 'a');
    assert(!synapse_mdns_txt_value(r, "id", &value, &length));
    observed++;
    return observed < stop_after;
}

static synapse_mdns_publication publication(void) {
    synapse_mdns_publication p = {0};
    snprintf(p.type, sizeof(p.type), "%s", "_fixture-launcher._tcp");
    snprintf(p.instance, sizeof(p.instance), "%s", "Inference node");
    p.port = 18080;
    p.txt_count = 2;
    p.txt[0].length = 14; memcpy(p.txt[0].bytes, "engine=fixture", 14);
    const uint8_t blob[] = {'b','l','o','b','=',0,1};
    p.txt[1].length = sizeof(blob); memcpy(p.txt[1].bytes, blob, sizeof(blob));
    return p;
}

int main(void) {
    synapse_mdns_browse_report report;
    assert(synapse_mdns_browse("bad", NULL, 100, observe, NULL, &report) < 0); clean();
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 0, observe, NULL, &report) < 0); clean();
    for (int stage = 0; stage < 3; stage++) {
        if (stage == 0) fail_poll = 1; else if (stage == 1) fail_client = 1; else fail_browser = 1;
        assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 100, observe, NULL, &report) < 0);
        assert(!report.available); clean();
    }
    observed = 0; browse_mode = 0;
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 1000, observe, NULL, &report) == 0);
    assert(observed == 3 && report.observations == 3); clean();
    observed = 0; stop_after = 1;
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 1000, observe, NULL, &report) == 0);
    assert(observed == 1); clean(); stop_after = 3;
    browse_mode = 2;
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 300, observe, NULL, &report) == 0);
    assert(report.limited && !report.observations); clean();
    browse_mode = 3;
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 300, observe, NULL, &report) < 0); clean();
    browse_mode = 4; observed = 0;
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 300, observe, NULL, &report) == 0);
    assert(!report.limited && observed == 3); clean(); /* no first-16-key truncation */
    browse_mode = 6; observed = 0;
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 300, observe, NULL, &report) == 0);
    assert(report.limited && observed == 3); clean();
    browse_mode = 5;
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 300, observe, NULL, &report) == 0);
    assert(report.resolver_failures == 3 && !report.observations); clean();
    browse_mode = 0; fail_iterate = 1;
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 300, observe, NULL, &report) < 0); clean();
    browse_mode = 0; fail_resolver = 1;
    assert(synapse_mdns_browse("_fixture-launcher._tcp", NULL, 300, observe, NULL, &report) == 0);
    assert(report.resolver_failures == 3); clean();

    synapse_mdns_publication pub = publication();
    synapse_mdns_publisher *p = NULL; char reason[64]; bool committed; uint32_t retries;
    pub.txt_count = 17;
    assert(synapse_mdns_publish_start(&p, &pub, 100, true, reason, sizeof(reason)) < 0 && !p); clean();
    pub = publication();
    pub.txt[0].length = 256;
    assert(synapse_mdns_publish_start(&p, &pub, 100, false, reason, sizeof(reason)) < 0 && !p); clean();
    pub = publication();
    pub.port = 0;
    assert(synapse_mdns_publish_start(&p, &pub, 100, false, reason, sizeof(reason)) < 0 && !p); clean();
    pub = publication();
    assert(synapse_mdns_publish_start(&p, &pub, 0, false, reason, sizeof(reason)) == 0);
    assert(strcmp(reason, "pending") == 0);
    synapse_mdns_publisher *same = p;
    assert(synapse_mdns_publish_start(&p, &pub, 0, false, reason, sizeof(reason)) < 0 && p == same);
    /* Publication owns a snapshot, not the caller's mutable buffers. */
    memset(&pub, 0, sizeof(pub));
    synapse_mdns_publish_iterate(p);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && committed);
    synapse_mdns_publish_stop(&p); clean();
    pub = publication();
    for (int stage = 0; stage < 5; stage++) {
        if (stage == 0) fail_poll = 1;
        else if (stage == 1) fail_client = 1;
        else if (stage == 2) fail_group = 1;
        else if (stage == 3) fail_publish = 1;
        else fail_commit = 1;
        assert(synapse_mdns_publish_start(&p, &pub, 100, false, reason, sizeof(reason)) < 0 && !p); clean();
    }
    sync_collision = 1;
    assert(synapse_mdns_publish_start(&p, &pub, 100, true, reason, sizeof(reason)) == 0);
    assert(strcmp(reason, "entry-collision") == 0);
    sync_collision = 0; now_ms += 3000;
    synapse_mdns_publish_iterate(p); synapse_mdns_publish_iterate(p);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && committed);
    /* A failed poll cannot leave a success latched in the live projection. */
    fail_iterate = 1;
    assert(synapse_mdns_publish_iterate(p) < 0);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && !committed);
    fail_iterate = 0; now_ms += 3000;
    synapse_mdns_publish_iterate(p); synapse_mdns_publish_iterate(p);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && committed);
    emit_group(AVAHI_ENTRY_GROUP_UNCOMMITED);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && !committed);
    now_ms += 3000; synapse_mdns_publish_iterate(p); synapse_mdns_publish_iterate(p);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && committed);
    synapse_mdns_publish_stop(&p); clean();
    fail_publish = 1;
    assert(synapse_mdns_publish_start(&p, &pub, 100, true, reason, sizeof(reason)) == 0);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && !committed);
    int before = additions;
    for (int i = 0; i < 10; i++) synapse_mdns_publish_iterate(p);
    assert(additions == before); /* no spinning retry */
    fail_publish = 0; now_ms += 3000;
    synapse_mdns_publish_iterate(p);
    synapse_mdns_publish_iterate(p);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && committed && retries == 1);
    assert(strcmp(sent.type, pub.type) == 0 && sent.port == pub.port && sent.txt_count == pub.txt_count);
    assert(sent.txt[1].length == pub.txt[1].length && memcmp(sent.txt[1].bytes, pub.txt[1].bytes, pub.txt[1].length) == 0);
    /* A real withdrawal of an already established group is not startup failure. */
    emit_group(AVAHI_ENTRY_GROUP_COLLISION);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && !committed);
    now_ms += 3000;
    synapse_mdns_publish_iterate(p); synapse_mdns_publish_iterate(p);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && committed && retries == 2);
    /* Avahi server re-registration and disconnect recreate owned resources. */
    assert(group && group->client);
    AvahiClient *c = group->client;
    c->state = AVAHI_CLIENT_S_REGISTERING;
    c->callback(c, c->state, c->data);
    now_ms += 3000; before = additions;
    synapse_mdns_publish_iterate(p);
    assert(additions == before);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && !committed);
    c->state = AVAHI_CLIENT_S_RUNNING;
    c->callback(c, c->state, c->data);
    now_ms += 3000;
    synapse_mdns_publish_iterate(p); synapse_mdns_publish_iterate(p);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && committed);
    c->error = AVAHI_ERR_DISCONNECTED; c->state = AVAHI_CLIENT_FAILURE;
    c->callback(c, c->state, c->data);
    now_ms += 3000; fail_client = 1;
    synapse_mdns_publish_iterate(p);
    assert(!group && alive_clients == 0);
    fail_client = 0; now_ms += 3000;
    synapse_mdns_publish_iterate(p); synapse_mdns_publish_iterate(p);
    assert(synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)) && committed);
    synapse_mdns_publish_stop(&p); synapse_mdns_publish_stop(&p); clean();
    assert(!synapse_mdns_publish_state(p, &committed, &retries, reason, sizeof(reason)));
    puts("generic mDNS: foreign service/TXT, bounded browse, cancellation, recovery: OK");
    return 0;
}
