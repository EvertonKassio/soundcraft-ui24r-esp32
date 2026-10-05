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
static std::map<String, String> restoredEchoes;
static std::map<int, std::vector<Change>> pendingRestores;
static size_t cursor = 0;
static bool awaiting = false;
static int restoringChannel = -1;
static uint16_t headphoneAuxMask = (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7);
static uint32_t channelColors[24], groupColors[6];
static String groupNames[6];
static uint8_t displayedGroups = 4;
static uint8_t displayedGroupMask = 0x0F;
uint8_t groupDisplayMask() { return displayedGroupMask; }
bool saveGroupDisplayMask(uint8_t mask) {
  mask &= 0x3F;
  if (!mask || prefs.putUChar("groupMask", mask) != 1) return false;
  displayedGroupMask = mask; return true;
}
static uint32_t visibleInputs = 0xFFFFFF;
String groupName(int group) { return groupNames[group]; }
uint8_t groupCount() { return displayedGroups; }
uint32_t visibleChannelMask() { return visibleInputs & 0xFFFFFF; }
uint8_t visibleGroupMask() { return (visibleInputs >> 24) & 0x3F; }
bool saveGroupCount(uint8_t count) {
  if (count < 1 || count > 6 || prefs.putUChar("groupCount", count) != 1) return false;
  displayedGroups = count; return true;
}
bool saveGroupName(int group, String name) {
  name.trim();
  if (group < 0 || group >= 6 || !name.length() || name.length() > 32) return false;
  if (prefs.putString(("gname" + String(group)).c_str(), name) != name.length()) return false;
  groupNames[group] = name; return true;
}
bool saveVisibleChannelMask(uint32_t mask) {
  mask &= 0x3FFFFFFF;
  if (__builtin_popcount(mask) > 24) { notice = "Limite de 24 botoes"; return false; }
  if (!(mask & 0xFFFFFF)) { notice = "Mantenha ao menos um canal visivel"; return false; }
  for (int i = 0; i < 24; ++i) if (selected[i] && !(mask & (1UL << i))) {
    notice = "Encerre o solo antes de ocultar o canal"; return false;
  }
  if (prefs.putUInt("visibleInputs", mask) != sizeof(mask)) return false;
  visibleInputs = mask; return true;
}
uint32_t groupColor(int group) { return groupColors[group]; }
bool saveGroupColor(int group, uint32_t color) {
  if (group < 0 || group >= 6) return false;
  if (prefs.putUInt(("gcolor" + String(group)).c_str(), color) != sizeof(color)) return false;
  groupColors[group] = color; return true;
}
static const uint32_t defaultColors[] = {0x2563EB, 0x7C3AED, 0xBE185D, 0xB45309, 0x15803D, 0x0E7490};
uint16_t soloAuxMask() { return headphoneAuxMask; }
bool saveSoloAuxMask(uint16_t mask) {
  if (soloActive || mixerBusy() || !(mask & 0x3FF)) { notice = "Encerre os solos e selecione algum AUX"; return false; }
  mask &= 0x3FF;
  if (prefs.putUShort("soloAux", mask) != sizeof(mask)) return false;
  headphoneAuxMask = mask; return true;
}
uint32_t channelColor(int ch) { return channelColors[ch]; }
bool saveChannelColor(int ch, uint32_t color) {
  if (ch < 0 || ch >= 24) return false;
  if (prefs.putUInt(("color" + String(ch)).c_str(), color) != sizeof(color)) return false;
  channelColors[ch] = color; return true;
}
static bool journalHasChannel(int ch) {
  String prefix = "i." + String(ch) + ".";
  for (const auto &c : journal) if (c.key.startsWith(prefix)) return true;
  return false;
}

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
static constexpr uint32_t WIFI_CONNECTION_TIMEOUT_MS = 10000;
static constexpr uint32_t MIXER_CONNECTION_TIMEOUT_MS = 10000;

bool connectionAttempting() { return connection == Connection::Wifi || connection == Connection::Mixer; }
String connectionProgress() {
  uint32_t limit = connection == Connection::Wifi ? WIFI_CONNECTION_TIMEOUT_MS : MIXER_CONNECTION_TIMEOUT_MS;
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
  // Retry transient association failures within the bounded connection window.
  WiFi.setAutoReconnect(true);
  if (!wifiConnected()) {
    WiFi.begin(ssid.c_str(), password.c_str());
    connectionSince = millis();
  }
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
    for (int a = 0; a < 10; ++a)
      if (s == "aux." + String(a) + ".mute" || s == "aux." + String(a) + ".post") return true;
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
      if (restoring || !pendingRestores.empty()) restoredEchoes[key] = val;
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
  if (!mixerReady() || recovery || ch < 0 || ch >= 24 || journalHasChannel(ch)) return false;
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
  if (!mixerReady() || recovery || group < 0 || group > 5) return false;
  int mask = mixerValue("mgmask").toInt(), bit = 1 << group;
  std::vector<Change> out;
  bool found = false;
  for (int i = 0; i < 24; ++i) {
    String membership = mixerValue(ik(i, "mgmask"));
    if (!membership.length()) { notice = "Aguardando grupos dos canais"; return false; }
    if (membership.toInt() & bit) {
      found = true;
      if (!journalHasChannel(i) && !add(out, ik(i, "forceunmute"), "0")) return false;
    }
  }
  if (!found) { notice = "Grupo sem entradas atribuidas"; return false; }
  if (!add(out, "mgmask", String(mask ^ bit))) return false;
  return sendControls(out);
}
bool toggleSolo(int ch) {
  if (!mixerReady() || recovery || ch < 0 || ch >= 24) return false;
  if (journalHasChannel(ch) && selected[ch]) {
    std::vector<Change> changes;
    String prefix = "i." + String(ch) + ".";
    for (auto it = journal.rbegin(); it != journal.rend(); ++it)
      if (it->key.startsWith(prefix)) changes.push_back({it->key, it->after, it->before});
    for (const auto &c : changes) restoredEchoes.erase(c.key);
    if (!sendControls(changes)) return false;
    pendingRestores[ch] = changes;
    selected[ch] = false;
    notice = ""; return true;
  }
  std::vector<Change> out;
  // Isolate every route BEFORE opening a previously muted input.
  if (!add(out, ik(ch, "mix"), "0")) return false;
  for (int a = 0; a < 10; ++a)
    if (!(headphoneAuxMask & (1 << a)) && !add(out, ik(ch, "aux." + String(a) + ".mute"), "1")) return false;
  for (int f = 0; f < 4; ++f)
    if (!add(out, ik(ch, "fx." + String(f) + ".mute"), "1")) return false;
  // Auxes are already pre-fader. Do not change pre/post or send levels.
  if (!add(out, ik(ch, "mute"), "0") || !add(out, ik(ch, "forceunmute"), "1")) return false;
  auto previous = journal;
  // A new tap during an unconfirmed restore reuses the original snapshot.
  String prefix = "i." + String(ch) + ".";
  for (auto &c : out) for (const auto &old : previous)
    if (old.key == c.key) { c.before = old.before; break; }
  journal.erase(std::remove_if(journal.begin(), journal.end(), [&](const Change &c) { return c.key.startsWith(prefix); }), journal.end());
  journal.insert(journal.end(), out.begin(), out.end());
  if (!persistJournal()) { journal = previous; notice = "Falha ao salvar recuperacao"; return false; }
  if (!sendControls(out)) {
    // Retain the journal if transport changes concurrently or storage rollback fails.
    journal = previous;
    if (!persistJournal()) { journal.insert(journal.end(), out.begin(), out.end()); recovery = true; }
    soloActive = !journal.empty(); return false;
  }
  pendingRestores.erase(ch);
  selected[ch] = true; soloActive = true; return true;
}
bool stopSolo() {
  if (!mixerReady() || !queue.empty() || !soloActive) return false;
  queue.clear(); restoringChannel = -1;
  // Reverse order: restore the main, FX, headphone modes and other input sends.
  for (auto it = journal.rbegin(); it != journal.rend(); ++it)
    queue.push_back({it->key, it->after, it->before});
  restoredEchoes.clear();
  cursor = 0; restoring = true; recovery = false; notice = "Restaurando..."; return true;
}
static void disconnected() {
  connected = false; values.clear();
  queue.clear(); pendingRestores.clear(); cursor = 0; awaiting = false;
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
  WiFi.setAutoReconnect(true); notice = "";
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
  headphoneAuxMask = prefs.getUShort("soloAux", headphoneAuxMask) & 0x3FF;
  if (!headphoneAuxMask) headphoneAuxMask = (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7);
  for (int i = 0; i < 24; ++i) channelColors[i] = prefs.getUInt(("color" + String(i)).c_str(), defaultColors[i % 6]);
  for (int i = 0; i < 6; ++i) groupColors[i] = prefs.getUInt(("gcolor" + String(i)).c_str(), defaultColors[i]);
  const char *names[] = {"VOCAL", "INSTRUMENTOS", "BATERIA", "SEM FIO", "GRUPO 5", "GRUPO 6"};
  for (int i = 0; i < 6; ++i) groupNames[i] = prefs.getString(("gname" + String(i)).c_str(), names[i]);
  displayedGroups = prefs.getUChar("groupCount", 4);
  if (displayedGroups < 1 || displayedGroups > 6) displayedGroups = 4;
  displayedGroupMask = prefs.getUChar("groupMask", (1 << displayedGroups) - 1) & 0x3F;
  if (!displayedGroupMask) displayedGroupMask = 0x0F;
  visibleInputs = prefs.getUInt("visibleInputs", 0xFFFFFF) & 0x3FFFFFFF;
  if (!visibleInputs) visibleInputs = 0xFFFFFF;
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    Serial.printf("[wifi] desconectado: motivo=%u\n", info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  loadJournal(); WiFi.mode(WIFI_STA); transportInit(); retryConnection();
}
String mixerStatus() {
  if (connectionAttempting()) return connectionProgress();
  if (connection == Connection::Paused) return notice;
  if (WiFi.status() != WL_CONNECTED) return "Wi-Fi desconectado";
  if (!connected) return "Conectando a mesa...";
  if (mixerBusy()) return restoring ? "Restaurando valores..." : recovery ? "Recuperando solo..." : "Aguardando confirmacao...";
  if (notice.length()) return notice;
  return mixerReady() ? soloActive ? "SOLO ATIVO | toque para restaurar" : "Mesa conectada" : "Sincronizando...";
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
    else if (now - connectionSince >= WIFI_CONNECTION_TIMEOUT_MS) { cancelConnection(); notice = "Wi-Fi: tempo esgotado. Confira rede/senha."; }
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
  // Confirm completed channel restores independently; never lock other inputs.
  for (auto it = pendingRestores.begin(); it != pendingRestores.end();) {
    bool complete = true;
    for (const auto &c : it->second)
      if (!same(c.before, c.after) && !same(restoredEchoes[c.key], c.after)) { complete = false; break; }
    if (!complete) { ++it; continue; }
    auto previous = journal;
    String prefix = "i." + String(it->first) + ".";
    journal.erase(std::remove_if(journal.begin(), journal.end(), [&](const Change &c) { return c.key.startsWith(prefix); }), journal.end());
    if (!persistJournal()) { journal = previous; ++it; continue; }
    it = pendingRestores.erase(it); soloActive = !journal.empty();
  }
  if (recovery && mixerReady()) { stopSolo(); }
  if (!connected || queue.empty()) return;
  if (cursor >= queue.size()) {
    // Restoration is sent without per-command waits, but keep the persistent
    // journal until the entire restored state has been observed from the mixer.
    if (restoring) {
      bool complete = true;
      for (const auto &c : queue)
        if (!same(c.before, c.after) && !same(restoredEchoes[c.key], c.after)) { complete = false; break; }
      if (!complete) {
        if (now - sentAt > 4000) notice = "Restauracao pendente; aguardando a mesa";
        return;
      }
      auto previous = journal;
      if (restoringChannel >= 0) {
        String prefix = "i." + String(restoringChannel) + ".";
        journal.erase(std::remove_if(journal.begin(), journal.end(), [&](const Change &c) { return c.key.startsWith(prefix); }), journal.end());
      } else journal.clear();
      if (!persistJournal()) { journal = previous; notice = "Falha ao salvar recuperacao"; return; }
      if (restoringChannel >= 0) selected[restoringChannel] = false;
      else for (bool &s : selected) s = false;
      soloActive = !journal.empty(); restoring = false; restoringChannel = -1;
    }
    queue.clear(); awaiting = false; notice = ""; return;
  }
  // Feed the same batch transport used by channel controls in bounded chunks.
  // A 24-input solo can exceed its 64-command queue, so retry busy chunks later.
  std::vector<String> commands;
  size_t end = min(queue.size(), cursor + (size_t)16);
  for (size_t n = cursor; n < end; ++n) {
    const Change &c = queue[n];
    if (!same(c.before, c.after)) commands.push_back("3:::SETD^" + c.key + "^" + c.after);
  }
  if (!transportSendBatch(commands)) return;
  cursor = end; sentAt = now;
}