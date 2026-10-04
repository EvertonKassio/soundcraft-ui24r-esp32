#include "mixer.h"
#include <WiFi.h>
#include "transport.h"
#include <Preferences.h>
#include <cmath>
#include <algorithm>

std::map<String, String> values;
bool selected[24] = {};
bool soloActive = false;
static Preferences prefs;
static String ssid, password, host, notice;
static bool connected = false, restoring = false, recovery = false;
static uint32_t lastRx, sentAt;
static std::vector<Change> journal, queue;
static size_t cursor = 0;
static bool awaiting = false;
static bool radioSleeping = false;
static bool scanning = false;
static bool scanQueued = false;
static uint32_t scanStartAt = 0;
static uint32_t scanSessionAt = 0, scanPassAt = 0;
static uint8_t scanAttempts = 0;
static constexpr uint32_t SCAN_CHANNEL_MS = 360;
static constexpr uint8_t SCAN_MAX_ATTEMPTS = 3;
static String scanNotice;
static std::vector<WifiNetwork> networks;
enum class Connection { Paused, Wifi, Mixer, Ready };
static Connection connection = Connection::Paused;
static uint32_t connectionSince = 0;
static constexpr uint32_t MIXER_CONNECTION_TIMEOUT_MS = 20000;

bool connectionAttempting() { return connection == Connection::Wifi || connection == Connection::Mixer; }
String connectionProgress() {
  uint32_t limit = connection == Connection::Wifi ? 8000 : MIXER_CONNECTION_TIMEOUT_MS;
  uint32_t elapsed = millis() - connectionSince;
  return String(connection == Connection::Wifi ? "Conectando Wi-Fi" : "Conectando a mesa") + " | " + String(elapsed >= limit ? 0 : (limit - elapsed + 999) / 1000) + " s";
}
String displayIP() { return wifiConnected() ? WiFi.localIP().toString() : String("--"); }
String gatewayIP() { return wifiConnected() ? WiFi.gatewayIP().toString() : String("--"); }
static void disconnected();
void cancelConnection() {
  connection = Connection::Paused; transportConfigure(host, false); disconnected();
  WiFi.setAutoReconnect(false);
  if (!wifiConnected()) WiFi.disconnect();
  notice = "Conexao pausada. Corrija ou tente novamente.";
}
void retryConnection() {
  // esp_wifi_connect has priority over a scan and can abort it.
  if (scanning) { notice = "Aguarde a busca de redes terminar."; return; }
  disconnected(); notice = ""; connectionSince = millis();
  connection = wifiConnected() ? Connection::Mixer : Connection::Wifi;
  transportConfigure(host, wifiConnected());
  WiFi.setAutoReconnect(false);
  if (!wifiConnected()) WiFi.begin(ssid.c_str(), password.c_str());
}

void mixerStandby(bool sleeping) {
  if (!sleeping && radioSleeping) {
    radioSleeping = false;
    WiFi.mode(WIFI_STA);
    retryConnection();
    Serial.println("[energia] Wi-Fi retomado por toque");
  } else if (sleeping && !radioSleeping && !mixerReady() && !scanning) {
    // cancelConnection preserves the persistent solo recovery journal.
    cancelConnection();
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_OFF);
    radioSleeping = true;
    Serial.println("[energia] Wi-Fi desligado durante repouso sem mesa");
  }
}

bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
bool wifiScanning() { return scanning; }
String wifiScanStatus() { return scanNotice; }
const std::vector<WifiNetwork> &wifiNetworks() { return networks; }
void wifiStartScan() {
  if (scanning) return;
  WiFi.scanDelete();
  scanAttempts = 0;
  // A user-requested scan exclusively owns the radio: stop mixer transport,
  // connection attempts and the current Wi-Fi link without erasing credentials.
  cancelConnection();
  WiFi.disconnect(false, false);
  scanQueued = true; scanning = true; scanStartAt = millis();
  scanSessionAt = scanStartAt;
  scanNotice = "Preparando busca de redes...";
  Serial.printf("[wifi-scan] inicio: wifi=%d conexao=%d\n", (int)WiFi.status(), (int)connection);
}
static void pollScan() {
  if (!scanning) return;
  if (scanQueued) {
    if (millis() - scanStartAt < 700) return;
    scanQueued = false;
    ++scanAttempts;
    scanNotice = "Buscando redes... " + String(scanAttempts) + "/" + SCAN_MAX_ATTEMPTS;
    scanPassAt = millis();
    int result = WiFi.scanNetworks(true, false, false, SCAN_CHANNEL_MS);
    Serial.printf("[wifi-scan] tentativa=%u inicio=%d wifi=%d\n", scanAttempts, result, (int)WiFi.status());
    if (result == WIFI_SCAN_RUNNING) return;
  }
  int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) return;
  Serial.printf("[wifi-scan] tentativa=%u resultado=%d duracao=%lu ms wifi=%d\n", scanAttempts, count, (unsigned long)(millis() - scanPassAt), (int)WiFi.status());
  if (count <= 0 && scanAttempts < SCAN_MAX_ATTEMPTS) {
    WiFi.scanDelete(); scanQueued = true; scanStartAt = millis();
    scanNotice = "Repetindo busca... " + String(scanAttempts + 1) + "/" + SCAN_MAX_ATTEMPTS;
    return;
  }
  scanning = false;
  // Connection deadlines are suspended during scanning, not consumed by it.
  connectionSince += millis() - scanSessionAt;
  if (awaiting) sentAt += millis() - scanSessionAt;
  networks.clear();
  for (int i = 0; i < count; ++i) {
    String name = WiFi.SSID(i);
    if (!name.length()) continue;
    WifiNetwork entry{name, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN};
    auto existing = std::find_if(networks.begin(), networks.end(), [&](const WifiNetwork &n) { return n.ssid == name; });
    if (existing == networks.end()) networks.push_back(entry);
    else if (entry.rssi > existing->rssi) *existing = entry;
  }
  std::sort(networks.begin(), networks.end(), [](const WifiNetwork &a, const WifiNetwork &b) { return a.rssi > b.rssi; });
  scanNotice = count < 0 ? "Falha na busca. Tente novamente." : networks.empty() ? "Nenhuma rede encontrada" : String(networks.size()) + " redes encontradas";
  WiFi.scanDelete();
}

String mixerValue(const String &key) {
  auto it = values.find(key);
  return it == values.end() ? String() : it->second;
}
static String ik(int ch, const String &suffix) { return "i." + String(ch) + "." + suffix; }
static bool same(const String &a, const String &b) {
  return a.length() && b.length() && fabs(a.toDouble() - b.toDouble()) < 0.000001;
}
static bool track(const String &key) {
  if (key == "mgmask") return true;
  for (int i = 0; i < 24; ++i) {
    String p = "i." + String(i) + ".";
    if (!key.startsWith(p)) continue;
    String s = key.substring(p.length());
    return s == "name" || s == "mute" || s == "mix" || s == "mgmask" || s == "forceunmute" ||
      s == "aux.5.post" || s == "aux.6.post" || s == "fx.0.mute" ||
      s == "fx.1.mute" || s == "fx.2.mute" || s == "fx.3.mute";
  }
  for (int a = 0; a < 10; ++a) if (key == "a." + String(a) + ".mute") return true;
  return false;
}
// Accept both real newline-delimited packets and the space-separated capture.
static void parse(const String &data) {
  int pos = 0;
  while (pos < (int)data.length()) {
    auto tokenAt = [&](int offset) {
      return offset + 5 <= (int)data.length() && data[offset] == 'S' && data[offset + 1] == 'E' && data[offset + 2] == 'T' &&
        (data[offset + 3] == 'D' || data[offset + 3] == 'S') && data[offset + 4] == '^';
    };
    while (pos < (int)data.length() && !tokenAt(pos)) ++pos;
    if (pos >= (int)data.length()) break;
    int begin = pos;
    int sep = data.indexOf('^', begin + 5);
    if (sep < 0) break;
    int end = sep + 1;
    while (end < (int)data.length() && data[end] != '\n' && data[end] != '\t' && !tokenAt(end)) ++end;
    String key = data.substring(begin + 5, sep);
    if (!track(key)) { pos = end; continue; }
    String val = data.substring(sep + 1, end);
    int cut = val.indexOf('\n'); if (cut >= 0) val = val.substring(0, cut);
    cut = val.indexOf('\t'); if (cut >= 0) val = val.substring(0, cut);
    val.trim();
    if (track(key)) {
      values[key] = val;
    }
    if (awaiting && cursor < queue.size() && key == queue[cursor].key && same(val, queue[cursor].after)) {
      Serial.printf("[mesa-net] confirmado: %s=%s\n", key.c_str(), val.c_str());
      awaiting = false; ++cursor;
    }
    pos = end;
  }
}
static bool persistJournal() {
  String data;
  for (const auto &c : journal) data += c.key + "\t" + c.before + "\t" + c.after + "\n";
  return prefs.putString("journal", data) == data.length();
}
static void loadJournal() {
  String data = prefs.isKey("journal") ? prefs.getString("journal", "") : String();
  int pos = 0;
  while (pos < (int)data.length()) {
    int a = data.indexOf('\t', pos), b = data.indexOf('\t', a + 1), e = data.indexOf('\n', b + 1);
    if (a < 0 || b < 0 || e < 0) break;
    journal.push_back({data.substring(pos, a), data.substring(a + 1, b), data.substring(b + 1, e)});
    pos = e + 1;
  }
  recovery = !journal.empty(); soloActive = recovery;
}
bool mixerBusy() { return !queue.empty() || recovery; }
bool mixerReady() { return connected && millis() - lastRx < 10000 && mixerValue("mgmask").length(); }
static bool add(std::vector<Change> &out, String key, String target) {
  String before = mixerValue(key);
  if (!before.length()) { notice = "Aguardando estado: " + key; return false; }
  out.push_back({key, before, target}); return true;
}
static bool sendControls(const std::vector<Change> &changes) {
  std::vector<String> commands;
  for (const auto &change : changes)
    if (!same(mixerValue(change.key), change.after)) commands.push_back("3:::SETD^" + change.key + "^" + change.after);
  if (!transportSendBatch(commands)) { notice = "Envio ocupado. Tente novamente."; return false; }
  // Like the mixer's browser UI, apply accepted controls locally. Subsequent
  // SETD/SETS messages reconcile this state without requesting a full INIT.
  for (const auto &change : changes) values[change.key] = change.after;
  notice = ""; return true;
}
bool muteChannel(int ch) {
  if (!mixerReady() || mixerBusy() || soloActive || ch < 0 || ch >= 24) return false;
  String mute = mixerValue(ik(ch, "mute")), mask = mixerValue(ik(ch, "mgmask"));
  String force = mixerValue(ik(ch, "forceunmute"));
  if (!mute.length() || !mask.length() || !force.length()) return false;
  bool grouped = (mask.toInt() & mixerValue("mgmask").toInt()) != 0;
  bool closed = mute.toInt() || (grouped && !force.toInt());
  std::vector<Change> out;
  if (!add(out, ik(ch, "mute"), closed ? "0" : "1") ||
      !add(out, ik(ch, "forceunmute"), closed && grouped ? "1" : "0")) return false;
  return sendControls(out);
}
bool muteGroup(int group) {
  if (!mixerReady() || mixerBusy() || soloActive || group < 0 || group > 3) return false;
  int mask = mixerValue("mgmask").toInt(), bit = 1 << group;
  std::vector<Change> out;
  bool found = false;
  for (int i = 0; i < 24; ++i) {
    String membership = mixerValue(ik(i, "mgmask"));
    if (!membership.length()) { notice = "Aguardando grupos dos canais"; return false; }
    if (membership.toInt() & bit) {
      found = true;
      if (!add(out, ik(i, "forceunmute"), "0")) return false;
    }
  }
  if (!found) { notice = "Grupo sem entradas atribuidas"; return false; }
  if (!add(out, "mgmask", String(mask ^ bit))) return false;
  return sendControls(out);
}
bool startSolo() {
  if (!mixerReady() || mixerBusy() || soloActive) return false;
  std::vector<Change> out;
  bool any = false;
  // Mute the eight output masters before changing selected input faders.
  for (int a = 0; a < 10; ++a) if (a != 5 && a != 6)
    if (!add(out, "a." + String(a) + ".mute", "1")) return false;
  for (int i = 0; i < 24; ++i) if (selected[i]) {
    any = true;
    // Keep headphone send levels; temporarily use pre-fader so main=0 is inaudible there only.
    if (!add(out, ik(i, "aux.5.post"), "0") || !add(out, ik(i, "aux.6.post"), "0")) return false;
    // Block the selected input's wet signal from reaching main via FX returns.
    for (int f = 0; f < 4; ++f) if (!add(out, ik(i, "fx." + String(f) + ".mute"), "1")) return false;
    if (!add(out, ik(i, "mix"), "0")) return false;
  }
  if (!any) { notice = "Selecione ao menos um canal"; return false; }
  journal = out;
  if (!persistJournal()) { journal.clear(); notice = "Falha ao salvar recuperacao"; return false; }
  soloActive = true; queue = out; cursor = 0; notice = "Ativando solo..."; return true;
}
bool stopSolo() {
  if (!mixerReady() || !queue.empty() || !soloActive) return false;
  queue.clear();
  // Reverse order: restore channels while auxiliary masters remain muted.
  for (auto it = journal.rbegin(); it != journal.rend(); ++it)
    queue.push_back({it->key, it->after, it->before});
  cursor = 0; restoring = true; recovery = false; notice = "Restaurando..."; return true;
}
static void disconnected() {
  connected = false; values.clear();
  queue.clear(); cursor = 0; awaiting = false;
  recovery = !journal.empty(); restoring = false;
}
static void event(WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_DISCONNECTED) { disconnected(); return; }
  if (type == WStype_PONG) { lastRx = millis(); return; }
  if (type == WStype_CONNECTED) {
    connected = true; lastRx = millis(); notice = "";
    transportSend("3:::INIT"); transportSend("3:::ALIVE"); return;
  }
  if (type != WStype_TEXT) return;
  String data; data.reserve(length + 1);
  for (size_t i = 0; i < length; ++i) data += (char)payload[i];
  lastRx = millis();
  if (data.startsWith("1::") && !connected) { connected = true; notice = ""; transportSend("3:::INIT"); transportSend("3:::ALIVE"); }
  parse(data.startsWith("3:::") ? data.substring(4) : data);
}
String wifiSSID() { return ssid; }
String mixerHost() { return host; }
bool saveNetwork(const String &s, const String &p, const String &h) {
  if (scanning || soloActive || mixerBusy() || !s.length() || s.length() > 32 || p.length() > 63 || !h.length()) return false;
  for (unsigned i = 0; i < h.length(); ++i) {
    char c = h[i]; if (!(isalnum(c) || c == '.' || c == '-')) return false;
  }
  // Single NVS value avoids partially updated credentials after power failure.
  if (s.indexOf('\n') >= 0 || p.indexOf('\n') >= 0) return false;
  String config = s + "\n" + p + "\n" + h;
  if (prefs.putString("network", config) != config.length()) return false;
  ssid = s; password = p; host = h;
  transportConfigure(host, false); disconnected(); WiFi.disconnect();
  WiFi.setAutoReconnect(false); notice = "";
  connection = Connection::Wifi; connectionSince = millis();
  WiFi.begin(ssid.c_str(), password.c_str()); return true;
}
bool saveNetworkKeepingPassword(const String &s, const String &p, const String &h, bool keep) {
  return saveNetwork(s, keep ? password : p, h);
}
void mixerInit() {
  prefs.begin("ui24r", false);
  String config = prefs.getString("network", "Soundcraft Ui24\n\n10.10.1.1");
  int a = config.indexOf('\n'), b = config.indexOf('\n', a + 1);
  ssid = config.substring(0, a); password = config.substring(a + 1, b); host = config.substring(b + 1);
  loadJournal(); WiFi.mode(WIFI_STA); transportInit(); retryConnection();
}
String mixerStatus() {
  if (connectionAttempting()) return connectionProgress();
  if (connection == Connection::Paused) return notice;
  if (WiFi.status() != WL_CONNECTED) return "Wi-Fi desconectado";
  if (!connected) return "Conectando a mesa...";
  if (mixerBusy()) return restoring ? "Restaurando valores..." : recovery ? "Recuperando solo..." : "Aguardando confirmacao...";
  if (notice.length()) return notice;
  return mixerReady() ? soloActive ? "SOLO ATIVO | auxs 6/7" : "Mesa conectada" : "Sincronizando...";
}
void mixerLoop() {
  pollScan();
  transportPump(event);
  uint32_t now = millis();
  // Keep UI/event processing alive, but never start Wi-Fi reconnection mid-scan.
  if (scanning) return;
  if (connection == Connection::Paused) return;
  if (connection == Connection::Wifi) {
    if (wifiConnected()) { connection = Connection::Mixer; connectionSince = now; transportConfigure(host, true); }
    else if (now - connectionSince >= 8000) { cancelConnection(); notice = "Wi-Fi: tempo esgotado. Confira rede/senha."; }
    return;
  }
  if (!wifiConnected()) { retryConnection(); return; }
  if (connection == Connection::Mixer) {
    if (mixerReady()) {
      connection = Connection::Ready;
      Serial.println("[mesa-net] sincronizacao concluida: mgmask recebido");
    }
    else if (now - connectionSince >= MIXER_CONNECTION_TIMEOUT_MS) {
      bool socketConnected = connected;
      Serial.printf("[mesa-net] tempo esgotado: websocket=%d parametros=%u mgmask=%s\n", connected, (unsigned)values.size(), mixerValue("mgmask").c_str());
      cancelConnection();
      notice = socketConnected ? "Mesa conectada, mas sem sincronizacao." : "Mesa: tempo esgotado. Confira o IP.";
      return;
    }
  }
  if (connection == Connection::Ready && (!connected || now - lastRx > 10000)) {
    Serial.printf("[mesa-net] reconectando: websocket=%d sem recepcao ha %lu ms\n", connected, (unsigned long)(now - lastRx));
    retryConnection(); return;
  }
  if (recovery && mixerReady()) { stopSolo(); }
  if (!connected || queue.empty()) return;
  if (cursor >= queue.size()) {
    queue.clear(); awaiting = false;
    if (restoring) {
      if (!prefs.remove("journal")) { recovery = true; notice = "Falha ao limpar recuperacao"; return; }
      journal.clear(); soloActive = false; restoring = false;
    }
    notice = ""; return;
  }
  if (awaiting) {
    if (now - sentAt > 4000) {
      const Change &pending = queue[cursor];
      Serial.printf("[mesa-net] reconectando por falta de confirmacao: %s esperado=%s recebido=%s\n", pending.key.c_str(), pending.after.c_str(), mixerValue(pending.key).c_str());
      retryConnection();
    }
    return;
  }
  Change &c = queue[cursor];
  if (same(mixerValue(c.key), c.after)) { ++cursor; return; }
  awaiting = true; sentAt = now;
  String command = "3:::SETD^" + c.key + "^" + c.after;
  Serial.printf("[mesa-net] enviando: %s\n", command.c_str());
  if (!transportSend(command)) {
    Serial.println("[mesa-net] reconectando: falha ao enfileirar comando");
    retryConnection();
  }
}
