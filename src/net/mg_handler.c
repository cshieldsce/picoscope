#include "mg_handler.h"
#include "third_party/mongoose.h" 
#include "core/command_handler.h"

#include "pico/stdlib.h"
#include "net/frontend.h"
#include "FreeRTOS.h"
#include <string.h>
#include <stdio.h>

/* Define the global variables */
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

/* Compare request URI with a literal path */
static inline bool bUriEquals(const struct mg_http_message *hm, const char *s) {
    size_t n = strlen(s);
    return hm->uri.len == n && memcmp(hm->uri.buf, s, n) == 0;
}

/* Mongoose event handler */
void vEventHandler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
    (void) fn_data;
    /* Handle events */
    switch (ev) {
        case MG_EV_HTTP_MSG: {
            struct mg_http_message *hm = (struct mg_http_message *) ev_data;
            /* Serve HTML page or upgrade to WebSocket */
            if (bUriEquals(hm, "/")) {
                extern const char frontend_index_html[];  // your HTML
                mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", frontend_index_html);
            } else if (bUriEquals(hm, "/ws")) {
                mg_ws_upgrade(c, hm, NULL);
            } else {
                mg_http_reply(c, 404, "", "Not found");
            }
            break;
        }
        case MG_EV_WS_OPEN:
            vWebsocketAdd(c);
            break;
        case MG_EV_WS_MSG: {
            struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
            
            // Handle ping/pong
            if (wm->data.len > 5 && memcmp(wm->data.buf, "ping ", 5) == 0) {
                mg_ws_send(c, wm->data.buf, wm->data.len, WEBSOCKET_OP_TEXT);
                break;
            }
            
            // Parse JSON command
            char json[256];
            size_t n = wm->data.len < sizeof(json)-1 ? wm->data.len : sizeof(json)-1;
            memcpy(json, wm->data.buf, n);
            json[n] = '\0';
            
            // Example: {"cmd":"trigger_level","value":1.65}
            char *cmd_str = mg_json_get_str(mg_str(json), "$.cmd");
            if (cmd_str != NULL) {
                double value = 0.0;
                mg_json_get_num(mg_str(json), "$.value", &value);
        
                ScopeCommand_t xCmd = {0};
                ScopeStatus_t xStatus = {0};
        
                // Map command string to CommandType_e and set value
                if (strcmp(cmd_str, "trigger_level") == 0) {
                    xCmd.eType = CMD_TRIGGER_LEVEL;
                    xCmd.uValue.fTriggerLevel = (float)value;
                    bCommandHandlerExecute(&xCmd, &xStatus);
                } else if (strcmp(cmd_str, "trigger_mode") == 0) {
                    xCmd.eType = CMD_TRIGGER_MODE;
                    xCmd.uValue.eTriggerMode = (TriggerMode_e)((int)value);
                    bCommandHandlerExecute(&xCmd, &xStatus);
                } else if (strcmp(cmd_str, "trigger_edge") == 0) {
                    xCmd.eType = CMD_TRIGGER_EDGE;
                    xCmd.uValue.eTriggerEdge = (TriggerEdge_e)((int)value);
                    bCommandHandlerExecute(&xCmd, &xStatus);
                } else if (strcmp(cmd_str, "timebase_scale") == 0) {
                    xCmd.eType = CMD_TIMEBASE_SCALE;
                    xCmd.uValue.fTimePerDiv = (float)value;
                    bCommandHandlerExecute(&xCmd, &xStatus);
                } else if (strcmp(cmd_str, "run_stop") == 0) {
                    xCmd.eType = CMD_RUN_STOP;
                    xCmd.uValue.bRunning = ((int)value != 0);
                    bCommandHandlerExecute(&xCmd, &xStatus);
                } else {
                    snprintf(xStatus.acMessage, sizeof(xStatus.acMessage), "Unknown command: %s", cmd_str);
                    xStatus.bSuccess = false;
                }
        
                // Send response
                char resp[128];
                snprintf(resp, sizeof(resp), 
                         "{\"success\":%s,\"msg\":\"%s\"}", 
                         xStatus.bSuccess ? "true" : "false",
                         xStatus.acMessage);
                mg_ws_send(c, resp, strlen(resp), WEBSOCKET_OP_TEXT);
                
                mg_free(cmd_str);  // Free with Mongoose allocator
            }
            break;
        }
        case MG_EV_CLOSE:
            if (c->is_websocket) vWebsocketRemove(c);
            break;
        default:
            break;
    }
}

/* Mongoose allocators (required) - choose C library or FreeRTOS heap */
void *mg_calloc(size_t cnt, size_t size) {
    size_t total = cnt * size;
    void *ptr = pvPortMalloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}
void mg_free(void *ptr) { vPortFree(ptr); }