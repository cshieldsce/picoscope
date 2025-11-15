#ifndef MG_HANDLER_H
#define MG_HANDLER_H

#include "third_party/mongoose.h"

#define DISPLAY_POINTS 256

/* Binary packet for WebSocket streaming; packed to avoid any padding bytes. */
typedef struct __attribute__((packed)) {
    uint32_t ulTimestampMs;
    uint32_t ulAgeMs;
    uint32_t ulSampleCount;
    float vmin;
    float vmax;
    float vavg;
    uint16_t usSamples[DISPLAY_POINTS];
} ScopePacket_t;

/* WebSocket connection tracking - declare as extern so they can be accessed from other files */
extern struct mg_mgr xWebsocketManager;
extern struct mg_connection *xWebsocketConnections[4];
extern size_t xWebsocketCount;

/* Event handler function - remove static so it can be used externally */
void vEventHandler(struct mg_connection *c, int ev, void *ev_data, void *fn_data);

#endif /* MG_HANDLER_H */

