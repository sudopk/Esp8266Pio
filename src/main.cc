#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <ESP_EEPROM.h>
#include <TaskScheduler.h>
#include <WiFiClient.h>

#include <sstream>
#include <string>

void SerialControl();
void SerialControl2();
void SerialControl3();
void LedControl();
void WifiInfo();
void WifiRun();

constexpr char kTextPlain[] = "text/plain";
constexpr char kTextHtml[] = "text/html";
constexpr char kHostname[] = "myspot";

constexpr uint8_t kRelayPin = D4;

constexpr uint16_t kDefaultPulseMs = 50;


enum class OnOffState { kOn, kOff };

OnOffState relay_state = OnOffState::kOff;
OnOffState led_state = OnOffState::kOff;

Scheduler sch;
Task serial(1000, TASK_FOREVER, &SerialControl, &sch);
Task ledHandler(5000, TASK_FOREVER, &LedControl, &sch);
Task wifiInfo(20000, TASK_FOREVER, &WifiInfo, &sch);
Task wifiRun(5000, TASK_FOREVER, &WifiRun, &sch);

void PrintWithMillis(const char* log) {
  Serial.print(millis());
  Serial.print(": ");
  Serial.println(log);
}

struct EepromData {
  uint16_t pulse_ms;
} eeprom_data;

ESP8266WebServer server(80);

ESP8266WiFiMulti wifi_multi;
constexpr uint32_t kWifiConnectTimeoutMs = 5000;

void RedirectToHome() {
  server.sendHeader("Location", "/", /*first=*/true);
  server.send(302, kTextPlain, "");
}

void ResetWifi(ESP8266WiFiMulti& wifi_multi) {
  WiFi.disconnect(true);
  wifi_multi.cleanAPlist();
  ESP.eraseConfig();
  // ESP.restart();
}

void PrintArgs(ESP8266WebServer& server) {
  Serial.printf("Number of args: %d\n", server.args());

  for (int i = 0; i < server.args(); i++) {
    Serial.printf("Arg %d: %s -> %s\n", i + 1, server.argName(i).c_str(),
                  server.arg(i).c_str());
  }
}

void SetupResponseHandlers(ESP8266WebServer& server) {
  server.on("/", [&server]() {
    std::stringstream html;
    html << R"html(
        <html><body>
        <br/>
        <div style="justify-content: center;">
        <form action="relayon" method="get">
          <input type="submit" value="Relay On"
        style="height:200px;width:200px" />
        </form>
        <br/>
        <form action="relayoff" method="get">
          <input type="submit" value="Relay Off" style="height:200px;width:200px" />
        </form>
        <form action="addap" method="post">
          <label for="ssid">Ssid:</label><br>
          <input type="text" id="ssid" name="ssid"><br>
          <label for="pwd">Password:</label><br>
          <input type="password" id="pwd" name="pwd"><br><br>
          <input type="submit" value="Add AP" style="height:50px;width:200px" />
        </form>
        <form action="setpulse" method="get">
          <label for="ms">Pulse millisec:</label><br>
          <input type="text" id="ms" name="ms"><br>
          <input type="submit" value="Set pulse ms" style="height:50px;width:200px" />
        </form>
        <form action="resetwifi" method="get">
          <input type="submit" value="Reset wifi" style="height:50px;width:200px" />
        </form>
        <br/>
        <h3>Relay state:
        )html";
    html << (relay_state == OnOffState::kOn ? "On" : "Off");
    html << "</h3><br/><h3>IP address: " << WiFi.localIP().toString().c_str();
    html << "</h3><br/><h3>Pulse: " << eeprom_data.pulse_ms << " ms";
    html << "</h3></div></body></html>";
    server.send(200, kTextHtml, html.str().c_str());
  });

  server.on("/relayon", [&server]() {
    PrintWithMillis("Turning relay on");
    relay_state = OnOffState::kOn;
    digitalWrite(kRelayPin, HIGH);
    RedirectToHome();
  });

  server.on("/relayoff", [&server]() {
    PrintWithMillis("Turning relay off");
    relay_state = OnOffState::kOff;
    digitalWrite(kRelayPin, LOW);
    RedirectToHome();
  });

  server.on("/addap", [&server]() {
    auto ssid = server.arg("ssid");
    ssid.trim();
    Serial.println("Add AP called, with ssid: " + ssid);
    if (ssid.length() == 0) {
      server.send(400, kTextPlain, "Invalid request with empty ssid.");
      return;
    }

    std::stringstream html;
    html << "<html><body><p>";
    auto added = wifi_multi.addAP(ssid.c_str(), server.arg("pwd").c_str());
    html << (added ? "Added" : "Failed to add");
    html << (" AP with ssid: " + WiFi.SSID()).c_str();

    html << "</p></body></html>";
    server.send(200, kTextHtml, html.str().c_str());
  });

  server.on("/resetwifi", [&server]() {
    ResetWifi(wifi_multi);
    server.send(200, kTextPlain,
                "Wifi config removed. Please restart the ESP.");
  });

  server.on("/pulsems", [&server]() {
    auto pulsems = std::stoi(server.arg("ms").c_str());
    if (pulsems < 2 || pulsems > 10000) {
      server.send(400, kTextPlain, "Invalid pulse value: " + pulsems);
      return;
    }

    eeprom_data.pulse_ms = pulsems;
    EEPROM.put(0, eeprom_data);
    EEPROM.commit();

    RedirectToHome();
  });
}

void setup() {
  Serial.begin(115200);

  Serial.println();
  PrintWithMillis("Inside setup");

  EEPROM.begin(512);
  EEPROM.get(0, eeprom_data);
  if(eeprom_data.pulse_ms == 0) {
    eeprom_data.pulse_ms = kDefaultPulseMs;
    EEPROM.put(0, eeprom_data);
    EEPROM.commit();
  }

  WiFi.mode(WiFiMode_t::WIFI_AP_STA);
  WiFi.persistent(true);
  bool ready = WiFi.softAP(kHostname, "myspotshared8266");
  Serial.print("Wifi Ap: ");
  Serial.println(ready ? "Ready" : "Failed");

  if (MDNS.begin(kHostname)) {
    Serial.println("MDNS responder started");
  }
  wifiInfo.enable();
  wifiRun.enable();

  SetupResponseHandlers(server);
  server.begin();

  pinMode(kRelayPin, OUTPUT);

  pinMode(LED_BUILTIN, OUTPUT);
  ledHandler.enable();
}

void loop() {
  sch.execute();
  server.handleClient();
  MDNS.update();
}

void WifiInfo() {
  Serial.printf("Stations connected to soft-AP = %d, is persistent: %d\n",
                WiFi.softAPgetStationNum(), WiFi.getPersistent());

  Serial.printf("Soft-AP IP address: %s\n", WiFi.softAPIP().toString().c_str());

  Serial.printf("Local IP: %s\n", WiFi.localIP().toString().c_str());
}

void WifiRun() {
  auto status = wifi_multi.run();
  if (status != WL_CONNECTED) {
    Serial.printf("Wifi not connected: %d\n", status);
  }
}

void SerialControl() {
  PrintWithMillis("Before serial read");
  if (Serial.available()) {
    auto command = Serial.readString();
    if (command == "on") {
      // relay_command = RelayCommand::kOn;
    } else if (command == "off") {
      // relay_command = RelayCommand::kOff;
    }
    Serial.print("Read: ");
    Serial.println(command);
  }
  PrintWithMillis("After serial read");
}

void SerialControl2() { PrintWithMillis("Serial control 2"); }

void SerialControl3() { PrintWithMillis("Serial control 3"); }

void LedControl() {
  // PrintWithMillis("Turning led off");
  if (led_state == OnOffState::kOn) {
    led_state = OnOffState::kOff;
    // Turns Off on high.
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    led_state = OnOffState::kOn;
    // Turns on on low.
    digitalWrite(LED_BUILTIN, LOW);
  }
}
