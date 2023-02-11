#include <WiFiManager.h>
#include <ESP_EEPROM.h>




void WifiManagerSetup() {
  WiFiManager wifi_manager;
  bool ready = wifi_manager.autoConnect("Esp8266 setup", "setupsetup");
}
