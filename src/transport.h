#pragma once
#include <Arduino.h>
#include <WebSocketsClient.h>
void transportInit();
void transportConfigure(const String &host, bool enabled);
bool transportSend(const String &message);
void transportPump(void (*callback)(WStype_t, uint8_t *, size_t));
