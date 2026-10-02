// SPDX-License-Identifier: MIT
/* Public shared/static API test: only invalid network requests are invoked. */
#include <synapse/mdns.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(SYNAPSE_MDNS_API_MAJOR == 1U, "unsupported mDNS API header");

int main(void) {
    assert(synapse_mdns_api_major() == SYNAPSE_MDNS_API_MAJOR);
    synapse_mdns_record *record = calloc(1, sizeof(*record)); assert(record);
    const uint8_t wire[] = {4,'f','l','a','g', 6,'e','m','p','t','y','=',
                           7,'b','l','o','b','=',0,1, 4,'I','D','=','x', 4,'i','d','=','y'};
    memcpy(record->txt, wire, sizeof(wire)); record->txt_bytes = sizeof(wire); record->txt_count = 5;
    const uint8_t *value = NULL; size_t length = 0, offset = 0, entries = 0;
    while (synapse_mdns_txt_next(record, &offset, &value, &length)) entries++;
    assert(entries == 5 && offset == sizeof(wire));
    assert(synapse_mdns_txt_value(record, "flag", &value, &length) && !value && !length);
    assert(synapse_mdns_txt_value(record, "empty", &value, &length) && value && !length);
    assert(synapse_mdns_txt_value(record, "blob", &value, &length) && length == 2 && value[0] == 0 && value[1] == 1);
    assert(synapse_mdns_txt_value(record, "id", &value, &length) && length == 1 && value[0] == 'x');
    assert(!synapse_mdns_txt_value(record, "unknown", &value, &length));
    record->txt[0] = 255; record->txt_bytes = 1; offset = 0;
    assert(!synapse_mdns_txt_next(record, &offset, &value, &length));
    record->txt_bytes = SYNAPSE_MDNS_TXT_WIRE_MAX + 1U;
    assert(!synapse_mdns_txt_next(record, &offset, &value, &length));
    assert(!synapse_mdns_txt_next(NULL, &offset, &value, &length));
    free(record);

    synapse_mdns_browse_report report;
    assert(synapse_mdns_browse("bad", NULL, 1, NULL, NULL, &report) < 0);
    assert(!report.available && !report.observations && strcmp(report.reason, "invalid-request") == 0);
    synapse_mdns_publication publication = {0};
    synapse_mdns_publisher *publisher = NULL; char reason[64]; bool committed = false;
    assert(synapse_mdns_publish_start(&publisher, &publication, 0, false, reason, sizeof(reason)) < 0);
    assert(!publisher && strcmp(reason, "invalid-request") == 0);
    assert(synapse_mdns_publish_iterate(NULL) < 0);
    assert(!synapse_mdns_publish_state(NULL, &committed, NULL, reason, sizeof(reason)));
    synapse_mdns_publish_stop(&publisher); synapse_mdns_publish_stop(&publisher);
    synapse_mdns_publish_stop(NULL);
    puts("public mDNS API/ABI 1, raw TXT and fail-closed inputs: OK");
    return 0;
}
