#include "mg_handler.h"

#include "pico/stdlib.h"
#include "frontend.h"
#include "FreeRTOS.h"

/* Fix the conflict by undefining the poll macro */

/* Define the global variables (not static) */
struct mg_mgr xWebsocketManager;
struct mg_connection *xWebsocketConnections[4];
size_t xWebsocketCount = 0;

static void vWebsocketAdd(struct mg_connection *c) {
    if (xWebsocketCount < (sizeof xWebsocketConnections / sizeof xWebsocketConnections[0])) {
        xWebsocketConnections[xWebsocketCount++] = c;
        printf("WS client connected (%zu total)\n", xWebsocketCount);
    }
}

static void vWebsocketRemove(struct mg_connection *c) {
    for (size_t i = 0; i < xWebsocketCount; i++) {
        if (xWebsocketConnections[i] == c) {
            for (size_t j = i + 1; j < xWebsocketCount; j++) xWebsocketConnections[j - 1] = xWebsocketConnections[j];
            xWebsocketConnections[--xWebsocketCount] = NULL;
            printf("WS client disconnected (%zu total)\n", xWebsocketCount);
            break;
        }
    }
}

/* Compare request URI with a literal path (portable across Mongoose versions) */
static inline bool bUriEquals(const struct mg_http_message *hm, const char *s) {
    size_t n = strlen(s);
    return hm->uri.len == n && memcmp(hm->uri.buf, s, n) == 0;
}

/* Forward declaration for mg_http_listen */
void vEventHandler(struct mg_connection *c, int ev, void *ev_data, void *fn_data);

/* Mongoose event handler: serves HTML, upgrades WS, echoes text frames (for RTT) */
void vEventHandler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    (void) fn_data;
    /* Handle events */
    switch (ev) {
        /* HTTP request */
        case MG_EV_HTTP_MSG: {
            struct mg_http_message *hm = (struct mg_http_message *) ev_data;
            /* Serve HTML page or upgrade to WebSocket */
            if (bUriEquals(hm, "/")) {
                mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", s_html);
            } else if (bUriEquals(hm, "/ws")) {
                mg_ws_upgrade(c, hm, NULL);             /* WebSocket upgrade */
            } else {
                mg_http_reply(c, 404, "", "Not found"); /* Not found */
            }
            break;
        }
        /* WebSocket open */
        case MG_EV_WS_OPEN:
            vWebsocketAdd(c);
            break;
        /* WebSocket message */
        case MG_EV_WS_MSG: {
            struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
            /* Echo text frames to let the browser measure RTT (ping -> echo) */
            mg_ws_send(c, wm->data.buf, wm->data.len, WEBSOCKET_OP_TEXT);
            break;
        }
        /* WebSocket close */
        case MG_EV_CLOSE:
            if (c->is_websocket) vWebsocketRemove(c);
            break;
        default:
            break;
    }
}

void *mg_calloc(size_t cnt, size_t size) {
    size_t total = cnt * size;
    void *ptr = pvPortMalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void mg_free(void *ptr) {
    vPortFree(ptr);
}