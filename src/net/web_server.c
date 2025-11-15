#include "web_server.h"
#include "core/scope_data.h"
#include "mg_handler.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"

/* Min/max-aware decimation: preserves peaks */
static void vDecimateMinmax(const uint16_t* src, uint32_t src_len, uint16_t* dst, uint32_t dst_len) {
    if (dst_len == 0 || src_len == 0) {
        return;
    }
    uint32_t step = src_len / dst_len; if (step == 0) step = 1;
    for (uint32_t i = 0; i < dst_len; i++) {
        uint32_t start = i * step, end = start + step; if (end > src_len) end = src_len;
        uint16_t minv = 4095, maxv = 0;
        for (uint32_t j = start; j < end; j++) { uint16_t s = src[j]; if (s < minv) minv = s; if (s > maxv) maxv = s; }
        dst[i] = (i & 1u) ? maxv : minv;
    }
}

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
                xPacket.ulSampleCount = ADC_BUFFER_SIZE;
                xPacket.vmin = xLatest.min_voltage;
                xPacket.vmax = xLatest.max_voltage;
                xPacket.vavg = xLatest.avg_voltage;

                vDecimateMinmax(xLatest.pusSamples, ADC_BUFFER_SIZE, usDecimated, DISPLAY_POINTS);
                for (uint32_t i = 0; i < DISPLAY_POINTS; i++) xPacket.usSamples[i] = usDecimated[i];

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