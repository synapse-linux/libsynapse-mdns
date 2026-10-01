// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
/* Opt-in rig consumer of the neutral API. Not installed as a product command.
 * Build/link without Continuity, core, systemd or Wayland; accepts metadata
 * only (never credentials or application payloads). */
#include <synapse/mdns.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stopped;
static void stop_requested(int signal_number) { (void)signal_number; stopped = 1; }
static void json_string(const char *text) {
    fputc('"', stdout);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', stdout); fputc(*p, stdout); }
        else if (*p < 32) printf("\\u%04x", (unsigned)*p);
        else fputc(*p, stdout);
    }
    fputc('"', stdout);
}
static bool observe(const synapse_mdns_record *record, void *userdata) {
    (void)userdata;
    fputs("{\"type\":", stdout); json_string(record->type);
    fputs(",\"instance\":", stdout); json_string(record->instance);
    fputs(",\"host\":", stdout); json_string(record->host);
    fputs(",\"address\":", stdout); json_string(record->address);
    printf(",\"port\":%u,\"interface\":%d,\"family\":%d,\"txtCount\":%zu,\"txtTruncated\":%s}\n",
           (unsigned)record->port, record->interface_index, record->family, record->txt_count,
           record->txt_truncated ? "true" : "false");
    return true;
}
int main(int argc, char **argv) {
    if (synapse_mdns_api_major() != SYNAPSE_MDNS_API_MAJOR) {
        fputs("rig: unsupported libsynapse-mdns API major\n", stderr);
        return 2;
    }
    if (argc == 3 && strcmp(argv[1], "browse") == 0) {
        synapse_mdns_browse_report report;
        int rc = synapse_mdns_browse(argv[2], NULL, 3000, observe, NULL, &report);
        fprintf(stderr, "observations=%zu limited=%s failures=%zu reason=%s\n",
                report.observations, report.limited ? "true" : "false",
                report.resolver_failures, report.reason);
        return rc == 0 ? 0 : 2;
    }
    if (argc < 5 || strcmp(argv[1], "publish") != 0 || argc > 5 + (int)SYNAPSE_MDNS_TXT_MAX) {
        fputs("rig usage: browse <type> | publish <type> <instance> <port> [TXT...]\n", stderr);
        return 2;
    }
    char *end = NULL;
    unsigned long port = strtoul(argv[4], &end, 10);
    if (!end || *end || port == 0 || port > 65535) return 2;
    synapse_mdns_publication pub = {0};
    if (strlen(argv[2]) >= sizeof(pub.type) || strlen(argv[3]) >= sizeof(pub.instance)) return 2;
    snprintf(pub.type, sizeof(pub.type), "%s", argv[2]);
    snprintf(pub.instance, sizeof(pub.instance), "%s", argv[3]);
    pub.port = (uint16_t)port;
    for (int i = 5; i < argc; i++) {
        size_t length = strlen(argv[i]);
        if (length > SYNAPSE_MDNS_TXT_BYTES) return 2;
        pub.txt[pub.txt_count].length = length;
        memcpy(pub.txt[pub.txt_count++].bytes, argv[i], length);
    }
    (void)signal(SIGTERM, stop_requested);
    (void)signal(SIGINT, stop_requested);
    synapse_mdns_publisher *publisher = NULL;
    char reason[SYNAPSE_MDNS_REASON_CAP];
    if (synapse_mdns_publish_start(&publisher, &pub, 2000, true, reason, sizeof(reason)) < 0) {
        fprintf(stderr, "publish failed reason=%s\n", reason);
        return 2;
    }
    bool previous = false;
    char previous_reason[SYNAPSE_MDNS_REASON_CAP] = "";
    /* Rig tool has a finite lifetime even when the orchestrator disappears. */
    for (unsigned i = 0; i < 600 && !stopped; i++) {
        bool committed = false; uint32_t retries = 0;
        (void)synapse_mdns_publish_iterate(publisher);
        (void)synapse_mdns_publish_state(publisher, &committed, &retries, reason, sizeof(reason));
        if (committed != previous || strcmp(reason, previous_reason) != 0) {
            printf("{\"committed\":%s,\"retries\":%u,\"reason\":", committed ? "true" : "false", retries);
            json_string(reason); fputs("}\n", stdout); fflush(stdout);
            previous = committed;
            snprintf(previous_reason, sizeof(previous_reason), "%s", reason);
        }
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 100000000};
        (void)nanosleep(&delay, NULL);
    }
    synapse_mdns_publish_stop(&publisher);
    return 0;
}
