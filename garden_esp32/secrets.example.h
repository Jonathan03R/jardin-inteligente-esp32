#pragma once

// Copy to secrets.h, then replace these local values. Never commit secrets.h.
const char WIFI_SSID[] = "YOUR_WIFI";
const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
const char MQTT_HOST[] = ""; // Vacio: descubre automaticamente jardin.exe por mDNS.
const int MQTT_PORT = 1884;
const char MQTT_USER[] = "esp32_garden";
const char MQTT_PASSWORD[] = "YOUR_MQTT_PASSWORD";
const char DEVICE_ID[] = "garden-01";
