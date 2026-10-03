#include "transport.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

struct Config { uint32_t generation; bool enabled; char host[65]; };
struct Send { uint32_t generation; char text[256]; };
struct Incoming { uint32_t generation; WStype_t type; size_t length; uint8_t *data; };
static QueueHandle_t configs, outgoing, incoming;
static std::atomic<uint32_t> generation{0};
static std::atomic<bool> enabled{false};
static bool healthy = false;

// Only this task owns HTTP/TCP/WebSocket. LVGL and mixer state stay on the UI task.
static void worker(void *) {
  Config config{};
  WebSocketsClient ws;
  bool initialized = false, protocolConnected = false;
  uint32_t lastAttempt = 0, lastAlive = 0;
  ws.setReconnectInterval(1000);
  ws.onEvent([&](WStype_t type, uint8_t *data, size_t len) {
    if (!enabled || config.generation != generation.load()) return;
    if (type == WStype_DISCONNECTED) { initialized = false; protocolConnected = false; }
    if (type == WStype_TEXT && len >= 3 && data[0] == '2' && data[1] == ':' && data[2] == ':') {
      ws.sendTXT("2::"); return;
    }
    if (type == WStype_TEXT && len >= 3 && data[0] == '1' && data[1] == ':' && data[2] == ':') protocolConnected = true;
    if (type != WStype_TEXT && type != WStype_DISCONNECTED) return;
    Incoming item{config.generation, type, len, nullptr};
    if (len) {
      item.data = (uint8_t *)malloc(len);
      if (!item.data) { enabled = false; return; }
      memcpy(item.data, data, len);
    }
    if (xQueueSend(incoming, &item, 0) != pdTRUE) { free(item.data); enabled = false; }
  });
  for (;;) {
    Config next;
    if (xQueueReceive(configs, &next, 0) == pdTRUE) {
      ws.disconnect(); config = next; initialized = false; protocolConnected = false; lastAttempt = millis() - 1000;
    }
    if (enabled && config.enabled && config.generation == generation.load() && WiFi.status() == WL_CONNECTED) {
      if (!initialized && millis() - lastAttempt >= 1000) {
        lastAttempt = millis();
        HTTPClient http; http.setConnectTimeout(400); http.setTimeout(400);
        String url = "http://" + String(config.host) + "/socket.io/1/?t=" + String(millis());
        if (http.begin(url)) {
          int code = http.GET(); String response = code == 200 ? http.getString() : String(); http.end();
          int sep = response.indexOf(':');
          if (enabled && config.generation == generation.load() && sep > 0 && response.indexOf("websocket") >= 0) {
            String path = "/socket.io/1/websocket/" + response.substring(0, sep);
            ws.begin(config.host, 80, path.c_str(), ""); initialized = true;
          }
        }
      }
      if (initialized) ws.loop();
      Send send;
      // Limit the work per iteration so control requests always get a turn.
      for (int i = 0; i < 4 && xQueueReceive(outgoing, &send, 0) == pdTRUE; ++i)
        if (enabled && config.generation == generation.load() && send.generation == config.generation) ws.sendTXT(send.text);
      if (enabled && config.generation == generation.load() && protocolConnected && millis() - lastAlive >= 1000) { ws.sendTXT("3:::ALIVE"); lastAlive = millis(); }
    } else if (initialized) { ws.disconnect(); initialized = false; protocolConnected = false; }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
void transportInit() {
  configs = xQueueCreate(1, sizeof(Config)); outgoing = xQueueCreate(16, sizeof(Send)); incoming = xQueueCreate(32, sizeof(Incoming));
  healthy = configs && outgoing && incoming && xTaskCreatePinnedToCore(worker, "mesa-net", 8192, nullptr, 1, nullptr, 0) == pdPASS;
}
void transportConfigure(const String &host, bool active) {
  uint32_t next = ++generation; enabled = active && healthy;
  if (!healthy) return;
  Config config{next, active, {}}; host.toCharArray(config.host, sizeof(config.host)); xQueueOverwrite(configs, &config);
}
bool transportSend(const String &message) {
  if (!healthy || !enabled || message.length() >= 256) return false;
  Send send{generation.load(), {}}; message.toCharArray(send.text, sizeof(send.text)); return xQueueSend(outgoing, &send, 0) == pdTRUE;
}
void transportPump(void (*callback)(WStype_t, uint8_t *, size_t)) {
  if (!healthy) return;
  Incoming item;
  for (int i = 0; i < 8 && xQueueReceive(incoming, &item, 0) == pdTRUE; ++i) {
    if (enabled && item.generation == generation.load()) callback(item.type, item.data, item.length);
    free(item.data);
  }
}
