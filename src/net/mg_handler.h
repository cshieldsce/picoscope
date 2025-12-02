#ifndef MG_HANDLER_H
#define MG_HANDLER_H

// Do NOT include mongoose.h here to avoid leaking lwIP macros like poll

#include <stddef.h>
#include <stdint.h>

#define DISPLAY_POINTS 256

/* Forward declarations to avoid including mongoose.h in this header */
struct mg_mgr;
struct mg_connection;

/* Binary packet for WebSocket streaming; packed to avoid any padding bytes. */
typedef struct __attribute__((packed)) {
    uint32_t ulTimestampMs;      // 4 bytes, offset 0
    uint32_t ulAgeMs;            // 4 bytes, offset 4
    uint32_t ulSampleCount;      // 4 bytes, offset 8
    uint32_t ulSampleRateHz;     // 4 bytes, offset 12  ← MISSING!
    float vmin;                  // 4 bytes, offset 16
    float vmax;                  // 4 bytes, offset 20
    float vavg;                  // 4 bytes, offset 24
    uint16_t usSamples[DISPLAY_POINTS];  // 512 bytes starting at offset 28
} ScopePacket_t;

/* WebSocket connection tracking */
extern struct mg_mgr xWebsocketManager;
extern struct mg_connection *xWebsocketConnections[4];
extern size_t xWebsocketCount;

/* Event handler function */
void vEventHandler(struct mg_connection *c, int ev, void *ev_data, void *fn_data);

#endif /* MG_HANDLER_H */

