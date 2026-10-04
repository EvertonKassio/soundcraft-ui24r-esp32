#include "transport.h"
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

// Only this task owns TCP/WebSocket. LVGL and mixer state stay on the UI task.
static void worker(void *) {
  Config config{};
  WebSocketsClient ws;
  bool initialized = false, protocolConnected = false;
  bool firstText = true;
  uint32_t lastAlive = 0;
  ws.setReconnectInterval(1000);
  auto serviceOutgoing = [&]() {
    if (!enabled || config.generation != generation.load() || !protocolConnected) return;
    if (millis() - lastAlive >= 1000) { ws.sendTXT("3:::ALIVE"); lastAlive = millis(); }
    Send send;
    for (int i = 0; i < 4 && xQueueReceive(outgoing, &send, 0) == pdTRUE; ++i)
      if (send.generation == config.generation && !ws.sendTXT(send.text)) Serial.printf("[mesa-net] falha ao enviar: %s\n", send.text);
  };
  ws.onEvent([&](WStype_t type, uint8_t *data, size_t len) {
    if (!enabled || config.generation != generation.load()) return;
    if (type == WStype_CONNECTED) {
      firstText = true;
      protocolConnected = true;
      Serial.printf("[mesa-net] WebSocket conectado a %s:80\n", config.host);
    }
    if (type == WStype_DISCONNECTED) {
      protocolConnected = false;
      Serial.printf("[mesa-net] WebSocket desconectado de %s:80; nova tentativa automatica\n", config.host);
    }
    if (type == WStype_TEXT && len >= 3 && data[0] == '2' && data[1] == ':' && data[2] == ':') {
      ws.sendTXT("2::");
      // Forward the heartbeat too: the mixer task uses received activity to
      // detect a stalled connection, even when no parameters are changing.
    }
    if (type == WStype_TEXT && len >= 3 && data[0] == '1' && data[1] == ':' && data[2] == ':') protocolConnected = true;
    if (type != WStype_TEXT && type != WStype_DISCONNECTED && type != WStype_CONNECTED) return;
    if (type == WStype_TEXT && firstText) {
      firstText = false;
      Serial.printf("[mesa-net] primeira mensagem: %u bytes | %.*s\n", (unsigned)len, (int)(len < 160 ? len : 160), (const char *)data);
    }
    if (type == WStype_TEXT && !(len >= 3 && (data[0] == '1' || data[0] == '2') && data[1] == ':' && data[2] == ':')) {
      // Meter traffic is frequent and irrelevant to this display. Keep only
      // frames carrying parameters; never discard a mixed parameter frame.
      bool parameters = false;
      for (size_t i = 0; i + 5 <= len; ++i) {
        if (memcmp(data + i, "SETD^", 5) == 0 || memcmp(data + i, "SETS^", 5) == 0) { parameters = true; break; }
      }
      if (!parameters) {
        Incoming activity{config.generation, WStype_PONG, 0, nullptr};
        xQueueSend(incoming, &activity, 0);
        return;
      }
    }
    Incoming item{config.generation, type, len, nullptr};
    if (len) {
      item.data = (uint8_t *)malloc(len);
      if (!item.data) { Serial.println("[mesa-net] sem memoria para receber mensagem"); enabled = false; return; }
      memcpy(item.data, data, len);
    }
    // The initial snapshot can exceed the queue capacity. Wait for the UI to
    // consume it instead of disabling the transport or dropping mixer state.
    while (enabled && config.generation == generation.load()) {
      if (xQueueSend(incoming, &item, pdMS_TO_TICKS(10)) == pdTRUE) return;
      serviceOutgoing();
    }
    free(item.data);
  });
  for (;;) {
    Config next;
    if (xQueueReceive(configs, &next, enabled ? 0 : pdMS_TO_TICKS(100)) == pdTRUE) {
      ws.disconnect(); config = next; initialized = false; protocolConnected = false;
    }
    if (enabled && config.enabled && config.generation == generation.load() && WiFi.status() == WL_CONNECTED) {
      if (!initialized) {
        // Ui accepts its text protocol directly over WebSocket on port 80.
        // Do not require a separate Socket.IO HTTP session negotiation.
        Serial.printf("[mesa-net] conectando a ws://%s:80/\n", config.host);
        ws.begin(config.host, 80, "/", ""); initialized = true;
      }
      if (initialized) ws.loop();
      serviceOutgoing();
    } else if (initialized) { ws.disconnect(); initialized = false; protocolConnected = false; }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
void transportInit() {
  configs = xQueueCreate(1, sizeof(Config)); outgoing = xQueueCreate(64, sizeof(Send)); incoming = xQueueCreate(32, sizeof(Incoming));
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
bool transportSendBatch(const std::vector<String> &messages) {
  // The UI is the only producer; the worker can only free queue slots.
  if (!healthy || !enabled || uxQueueSpacesAvailable(outgoing) < messages.size()) return false;
  for (const auto &message : messages) if (message.length() >= 256) return false;
  for (const auto &message : messages) if (!transportSend(message)) return false;
  return true;
}
void transportPump(void (*callback)(WStype_t, uint8_t *, size_t)) {
  if (!healthy) return;
  Incoming item;
  for (int i = 0; i < 8 && xQueueReceive(incoming, &item, 0) == pdTRUE; ++i) {
    if (enabled && item.generation == generation.load()) callback(item.type, item.data, item.length);
    free(item.data);
  }
}
