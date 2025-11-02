#include "web_server.h"
#include "core/scope_data.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#include "FreeRTOS.h"
#include "task.h"

#include "third_party/mongoose.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define DISPLAY_POINTS 256

/* Binary xPacket for WebSocket streaming; packed to avoid any padding bytes. */

typedef struct __attribute__((packed)) {
    uint32_t ulTimestampMs;
    uint32_t ulAgeMs;
    uint32_t ulSampleCount;
    float vmin;
    float vmax;
    float vavg;
    uint16_t usSamples[DISPLAY_POINTS];
} ScopePacket_t;

/* WebSocket connection tracking */
static struct mg_mgr xWebsocketManager;
static struct mg_connection *xWebsocketConnections[4];
static size_t xWebsocketCount = 0;

/* Embedded HTML page with live metrics (Sample Rate, Age, RTT) */
static const char *s_html =
"<!DOCTYPE html><html><head><title>Picoscope</title>"
"<meta charset='utf-8'>"
"<style>"
"body{font-family:monospace;background:#0a0a0a;color:#0f0;margin:20px}"
"h2{margin:0 0 20px 0}"
"#info{background:#1a1a1a;border:1px solid #333;padding:14px;margin-bottom:10px}"
".row{display:flex;gap:30px;margin:4px 0}"
".label{color:#888;min-width:150px}"
".value{color:#0f0;font-weight:bold}"
"canvas{border:1px solid #333;background:#000;display:block}"
"#status{color:#888;margin-top:6px;font-size:12px;text-align:center}"
".error{color:#f44}"
"</style></head><body>"
"<h2>PICOSCOPE</h2>"
"<div id='info'>"
"<div class='row'><span class='label'>Vmin:</span><span id='vmin' class='value'>---</span></div>"
"<div class='row'><span class='label'>Vmax:</span><span id='vmax' class='value'>---</span></div>"
"<div class='row'><span class='label'>Vavg:</span><span id='vavg' class='value'>---</span></div>"
"<div class='row'><span class='label'>Vpp:</span><span id='vpp' class='value'>---</span></div>"
"<div class='row'><span class='label'>Sample Rate:</span><span id='sps' class='value'>---</span></div>"
"<div class='row'><span class='label'>Data Age:</span><span id='age' class='value'>---</span></div>"
"<div class='row'><span class='label'>Latency RTT:</span><span id='rtt' class='value'>---</span></div>"
"<div class='row'><span class='label'>Update Rate:</span><span id='fps' class='value'>--- Hz</span></div>"
"</div>"
"<canvas id='scope' width='800' height='300'></canvas>"
"<div id='status'>Connecting...</div>"
"<script>"
"const c=document.getElementById('scope'),x=c.getContext('2d'),W=c.width,H=c.height;"
"let last=Date.now(),frames=0,prevTs=null,ws=null;"
"function grid(){x.strokeStyle='#1a1a1a';x.lineWidth=1;"
"for(let i=0;i<=10;i++){x.beginPath();x.moveTo(0,i*H/10);x.lineTo(W,i*H/10);x.stroke();"
"x.beginPath();x.moveTo(i*W/10,0);x.lineTo(i*W/10,H);x.stroke();}}"
"function draw(s){x.fillStyle='#000';x.fillRect(0,0,W,H);grid();"
"x.strokeStyle='#0f0';x.lineWidth=2;x.beginPath();"
"for(let i=0;i<s.length;i++){const xx=i/(s.length-1)*W,yy=H-(s[i]/4095)*H;"
"if(i===0)x.moveTo(xx,yy);else x.lineTo(xx,yy);}x.stroke();}"
"function sendPing(){if(ws&&ws.readyState===1){ws.send('ping '+Date.now());}}"
"setInterval(sendPing,2000);"
"function connect(){ws=new WebSocket('ws://'+location.hostname+'/ws');"
"ws.binaryType='arraybuffer';"
"ws.onopen=()=>{document.getElementById('status').textContent='Connected'};"
"ws.onclose=()=>{document.getElementById('status').textContent='Disconnected';document.getElementById('status').className='error';setTimeout(connect,2000)};"
"ws.onerror=()=>{document.getElementById('status').textContent='Error';document.getElementById('status').className='error'};"
"ws.onmessage=e=>{"
" if(typeof e.data==='string'){"
"  const s=e.data;"
"  if(s.startsWith('ping ')){const t=parseInt(s.slice(5));if(!isNaN(t)){const r=Date.now()-t;document.getElementById('rtt').textContent=r+' ms';}}"
"  return;"
" }"
" const v=new DataView(e.data),ts=v.getUint32(0,true),age=v.getUint32(4,true),sc=v.getUint32(8,true),"
" vmin=v.getFloat32(12,true),vmax=v.getFloat32(16,true),vavg=v.getFloat32(20,true),"
" s=new Uint16Array(e.data,24,256);"
" draw(s);"
" document.getElementById('vmin').textContent=vmin.toFixed(3)+' V';"
" document.getElementById('vmax').textContent=vmax.toFixed(3)+' V';"
" document.getElementById('vavg').textContent=vavg.toFixed(3)+' V';"
" document.getElementById('vpp').textContent=(vmax-vmin).toFixed(3)+' V';"
" document.getElementById('age').textContent=age+' ms';"
" if(prevTs!==null){const dt=(ts-prevTs)/1000; if(dt>0){const sps=sc/dt; document.getElementById('sps').textContent=(sps/1000).toFixed(1)+' kSPS';}}"
" prevTs=ts;"
" frames++;const now=Date.now();if(now-last>=1000){document.getElementById('fps').textContent=frames+' Hz';frames=0;last=now;}"
"};}connect();"
"</script></body></html>";

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

/* Forward declaration for mg_http_listen */
static void vEventHandler(struct mg_connection *c, int ev, void *ev_data, void *fn_data);

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

/* Compare request URI with a literal path (portable across Mongoose versions) */
static inline bool bUriEquals(const struct mg_http_message *hm, const char *s) {
    size_t n = strlen(s);
    return hm->uri.len == n && memcmp(hm->uri.buf, s, n) == 0;
}

/* Mongoose event handler: serves HTML, upgrades WS, echoes text frames (for RTT) */
static void vEventHandler(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
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

    mg_mgr_init(&xWebsocketManager);
    struct mg_connection *uxListener = NULL;

    /* Initialize Mongoose HTTP server */
    cyw43_arch_lwip_begin();
    uxListener = mg_http_listen(&xWebsocketManager, "http://0.0.0.0:80", (mg_event_handler_t) vEventHandler, NULL);
    cyw43_arch_lwip_end();

    if (!uxListener) {
        printf("Mongoose listen failed\n");
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
            }
        }
    }
}