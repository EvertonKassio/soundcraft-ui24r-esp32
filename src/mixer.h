#pragma once
#include <Arduino.h>
#include <vector>
#include <map>
static constexpr int CHANNEL_COUNT = 26;
static constexpr uint32_t CHANNEL_MASK = (1UL << CHANNEL_COUNT) - 1;
String channelPrefix(int ch);
String channelLabel(int ch);

struct Change { String key, before, after; };
struct WifiNetwork { String ssid; int32_t rssi; bool secured; };
extern std::map<String, String> values;
extern bool selected[CHANNEL_COUNT];
extern bool soloActive;
void mixerInit();
void mixerLoop();
bool mixerReady();
bool mixerBusy();
String mixerStatus();
String mixerValue(const String &key);
bool muteChannel(int ch);
bool muteGroup(int group);
bool toggleSolo(int ch);
uint16_t soloAuxMask();
bool saveSoloAuxMask(uint16_t mask);
uint32_t channelColor(int ch);
bool saveChannelColor(int ch, uint32_t color);
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

uint32_t groupColor(int group);
bool saveGroupColor(int group, uint32_t color);

String groupName(int group);
uint8_t groupCount();
bool saveGroupName(int group, String name);
bool saveGroupCount(uint8_t count);
uint32_t visibleChannelMask();
bool saveVisibleChannelMask(uint32_t mask);

uint8_t visibleGroupMask();

uint8_t groupDisplayMask();
bool saveGroupDisplayMask(uint8_t mask);
