#pragma once
#include <Arduino.h>
#include <vector>
#include <map>

struct Change { String key, before, after; };
struct WifiNetwork { String ssid; int32_t rssi; bool secured; };
extern std::map<String, String> values;
extern bool selected[24];
extern bool soloActive;
void mixerInit();
void mixerLoop();
bool mixerReady();
bool mixerBusy();
String mixerStatus();
String mixerValue(const String &key);
bool muteChannel(int ch);
bool muteGroup(int group);
bool startSolo();
bool stopSolo();
String wifiSSID();
String mixerHost();
bool saveNetwork(const String &ssid, const String &pass, const String &host);
bool saveNetworkKeepingPassword(const String &ssid, const String &pass, const String &host, bool keep);
bool wifiConnected();
bool wifiScanning();
String wifiScanStatus();
const std::vector<WifiNetwork> &wifiNetworks();
void wifiStartScan();
bool connectionAttempting();
String connectionProgress();
void cancelConnection();
void retryConnection();
String displayIP();
String gatewayIP();
void mixerStandby(bool sleeping);
