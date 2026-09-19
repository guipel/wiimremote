#pragma once

#include <Arduino.h>
#include <WiFi.h>

typedef void (*WiFiConnectedCb)();
typedef void (*WiFiDisconnectedCb)();

void wifi_service_init(WiFiConnectedCb onConnected, WiFiDisconnectedCb onDisconnected);
void wifi_service_handle();
void wifi_service_start_scan();
void wifi_service_connect(const char* ssid, const char* pass);
void wifi_service_forget();
void wifi_service_reconnect();

bool wifi_service_is_connected();
int8_t wifi_service_get_rssi();
String wifi_service_get_ip();
String wifi_service_get_ssid();
