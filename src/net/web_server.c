#include "web_server.h"
#include "core/scope_data.h"
#include "core/trigger.h"
#include "core/command_handler.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"        // Include CYW43 (and async_context) first

#include "third_party/mongoose.h"   // Include mongoose AFTER cyw43 to avoid poll macro clash
#undef poll                          // Safety: do not let lwIP's poll macro leak further

#include "mg_handler.h"
#include "core/command_handler.h"

#include "FreeRTOS.h"
#include "task.h"

static bool bInitServer() {
    mg_mgr_init(&xWebsocketManager);
    struct mg_connection *uxListener = NULL;

    /* Initialize Mongoose HTTP server */
    cyw43_arch_lwip_begin();
    uxListener = mg_http_listen(&xWebsocketManager, "http://0.0.0.0:80", (mg_event_handler_t) vEventHandler, NULL);
    cyw43_arch_lwip_end();

    if (!uxListener) {
        return false;
    }
    return true;
} 

/* Task: Web server + streamer
 * Drives Mongoose (mg_mgr_poll) under CYW43 lwIP guards
 * Sends a frame immediately when notified by acquisition (xTaskNotifyGive)
 * Also rate-limits to ~20 FPS as a fallback
 */
void vWebServerTask(void *pvParameters) {
    cyw43_arch_enable_sta_mode();

    WifiCredentials_t *creds = (WifiCredentials_t *) pvParameters;

    printf("Connecting to WiFi SSID: %s\n", creds->pcWifiName);
    if (cyw43_arch_wifi_connect_timeout_ms(creds->pcWifiName, creds->pcWifiPass,
                                           CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("WiFi connect failed\n");
        vTaskDelete(NULL);
        return;
    }

    printf("IP Address: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));
    vTaskDelay(pdMS_TO_TICKS(200));

    if (!bInitServer()) {
        printf("Mongoose server initialization failed\n");
        vTaskDelete(NULL);
        return;
    }
    printf("Mongoose HTTP server listening on port 80\n");
    
    vCommandHandlerInit();

    TickType_t xLastUpdate = xTaskGetTickCount();
    const TickType_t xUpdatePeriod = pdMS_TO_TICKS(50);  // ~20 FPS fallback
    uint16_t usDecimated[DISPLAY_POINTS];

    for (;;) {
        // Block until either notified by acquisition OR timeout to keep UI alive
        uint32_t ulNotif = ulTaskNotifyTake(pdTRUE, xUpdatePeriod);
        bool bPushDueNotify = (ulNotif > 0);

        // Always service Mongoose regularly
        cyw43_arch_lwip_begin();
        mg_mgr_poll(&xWebsocketManager, 0);
        cyw43_arch_lwip_end();

        TickType_t now = xTaskGetTickCount();
        bool bPushDueTimer = (now - xLastUpdate) >= xUpdatePeriod;
        if (bPushDueTimer) xLastUpdate = now;

        if (xWebsocketCount > 0 && (bPushDueNotify || bPushDueTimer)) {
            ScopeBuffer_t xLatest;
            if (bGetLatestScopeData(&xLatest, true) && xLatest.pusSamples != NULL) {
                ScopePacket_t xPacket;
                uint32_t ulNowMs = to_ms_since_boot(get_absolute_time());
                xPacket.ulTimestampMs = xLatest.ulTimestamp;
                xPacket.ulAgeMs = (xLatest.ulTimestamp <= ulNowMs) ? (ulNowMs - xLatest.ulTimestamp) : 0;
                xPacket.ulSampleCount = DISPLAY_POINTS;  // Always sending DISPLAY_POINTS samples
                xPacket.ulSampleRateHz = ulAdcDmaGetMeasuredSampleRate();
                xPacket.vmin = xLatest.min_voltage;
                xPacket.vmax = xLatest.max_voltage;
                xPacket.vavg = xLatest.avg_voltage;

                TriggerResult_t res;
                TriggerConfig_t* trig = pxCommandHandlerGetTriggerConfig();
                trigger_build_frame(
                    xLatest.pusSamples,
                    ADC_BUFFER_SIZE,
                    xPacket.ulSampleRateHz,
                    trig,  // Use pointer from command handler
                    usDecimated,
                    DISPLAY_POINTS,
                    &res
                );

                // Debug: print trigger status occasionally
                static uint32_t debug_count = 0;
                if (++debug_count % 100 == 0) {
                    printf("Trig: %s at idx=%d, span=%lu samples, Fs=%lu Hz\n",
                           res.bTriggered ? "LOCK" : "FREE",
                           res.iTriggerIndex,
                           res.uLen,
                           xPacket.ulSampleRateHz);
                }

                // Copy decimated samples into packet
                for (uint32_t i = 0; i < DISPLAY_POINTS; i++) xPacket.usSamples[i] = usDecimated[i];
                
                // Send to all connected WebSocket clients
                cyw43_arch_lwip_begin();
                for (size_t i = 0; i < xWebsocketCount; i++) {
                    struct mg_connection *ws = xWebsocketConnections[i];
                    if (ws && ws->is_websocket) {
                        mg_ws_send(ws, (const char *) &xPacket, sizeof(xPacket), WEBSOCKET_OP_BINARY);
                    }
                }
                cyw43_arch_lwip_end();

                // Release the in-use buffer so the next ready buffer can be promoted
                vScopeDataReleaseBuffer();
            }
        }
    }
}