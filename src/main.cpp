#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <Update.h>

const char* ssid = "Esp32";
const char* password = "123456789";

boolean wifiConnected = false;

const int ledPin = 42;
const int ledPin2 = 31;
const int ledPin3 = 40;

const int EEPROM_SIZE = 512;
const int SSID_ADDRESS = 0;
const int PASSWORD_ADDRESS = 32;

String eeprom_ssid;
String eeprom_password;

bool scan_flag = false;
int numNetworks = 0;
uint32_t prev_millis = 0;
StaticJsonDocument<512> jsonDoc;
JsonArray networks = jsonDoc.to<JsonArray>();

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

int latestRandomValues[3] = {0, 0, 0}; 

void sendLatestRandomValues(AsyncWebSocketClient *client) {
  StaticJsonDocument<200> doc;
  doc["values"][0] = latestRandomValues[0];
  doc["values"][1] = latestRandomValues[1];
  doc["values"][2] = latestRandomValues[2];
  char buffer[200];
  size_t len = serializeJson(doc, buffer);
  client->text(buffer, len);
}

void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("Client connected");
    sendLatestRandomValues(client); 
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("Client disconnected");
  }
}

void sendRandomValues() {
  
  for (int i = 0; i < 3; i++) {
    latestRandomValues[i] = random(0, 100);
  }
}

String readEEPROMString(int start, int maxLen) {
  String result;
  for (int i = start; i < start + maxLen; ++i) {
    char c = EEPROM.read(i);
    if (c == '\0') break; 
    result += c;
  }
  return result;
}

void clearEEPROM() {
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
}

void writeEEPROMString(int start, String data) {
  int i;
  for (i = 0; i < data.length(); i++) {
    EEPROM.write(start + i, data[i]);
  }
  EEPROM.write(start + i, '\0');
  EEPROM.commit();
}

void handleCheck(AsyncWebServerRequest *request) {
  request->send(200, "application/json", "{\"status\":\"true\",\"message\":\"Mpu Connection\"}");
}

void handleScan(AsyncWebServerRequest *request) {
  String response;
  serializeJson(jsonDoc, response);

  request->send(200, "application/json", "{\"status\":\"true\",\"message\":\"Find Complated\", \"data\":" + response +"}" );
}

void handleDelete(AsyncWebServerRequest *request) {
  clearEEPROM();
  Serial.println("Saved network credentials deleted. Device now in AP+STA mode.");
  delay(1000);
  WiFi.disconnect(); 
  request->send(200, "application/json", "{\"status\":\"true\",\"message\":\"Network credentials deleted, device reset to AP+STA mode.\"}");
  delay(1000);
  ESP.restart();
}

void setup() 
{
  Serial.println("şimdi ben buradayım");
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(ledPin, OUTPUT);
  pinMode(ledPin2, OUTPUT); 

  Serial.println("bak buradayım");

  eeprom_ssid = readEEPROMString(0, 32); 
  int passwordStart = eeprom_ssid.length() + 1; 
  eeprom_password = readEEPROMString(passwordStart, 64 - passwordStart); 
  
  Serial.println(eeprom_ssid + eeprom_password);

  Serial.println("tamam da niye oradasın");
  if (!eeprom_ssid.isEmpty() && !eeprom_password.isEmpty()) {
    Serial.println("maalesefBuradavım");
    WiFi.mode(WIFI_AP_STA); 
    delay(1000);
    WiFi.begin(eeprom_ssid.c_str(), eeprom_password.c_str());
    Serial.println("hop diye buradayım");
    delay(3000);
    Serial.println(WiFi.status());

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi connection successful!");
      Serial.println(WiFi.status());
      Serial.println(WiFi.localIP());
      WiFi.mode(WIFI_STA);
      wifiConnected = true;
    }
  } else if (!wifiConnected) {
      Serial.println("evet geldim ve buradayım");

      WiFi.softAP(ssid, password);
      Serial.println("WiFi network started in SoftAP mode.");
      Serial.println(WiFi.softAPIP());
      wifiConnected = true;
  }
  
  server.on("/", HTTP_GET, handleCheck);

  server.on("/scan", HTTP_GET, handleScan);

  server.on("/delete", HTTP_DELETE, handleDelete);

  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, data);
    const char* ssid = doc["ssid"];
    const char* password = doc["password"];

    Serial.print("Connected SSID: ");
    Serial.println(ssid);
    Serial.print("Password: ");
    Serial.println(password);

    WiFi.begin(ssid, password);
    for(int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++){
      Serial.print(".");
      delay(100);
    }
    Serial.print(WiFi.status() == WL_CONNECTED);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected!");
      Serial.print(WiFi.localIP().toString());
      clearEEPROM();
      writeEEPROMString(0, ssid);
      int passwordStart = strlen(ssid) + 1;
      writeEEPROMString(passwordStart, password);
      request->send(200, "application/json", "{\"status\": \"true\",\"message\": \"Connection Successful.\",\"ip\": \"" + WiFi.localIP().toString() + "\"}");
      delay(1000);
      WiFi.mode(WIFI_MODE_STA);

    } else {
      request->send(500, "application/json", "{\"status\":\"false\",\"error\":\"Connection failed.\"}");
      WiFi.disconnect();
    }
  });

  server.on("/toggle/led", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("pin") && request->hasParam("state")) {
      String pinStr = request->getParam("pin")->value();
      String stateStr = request->getParam("state")->value();

      int pin = pinStr.toInt();
      bool state = stateStr == "true" ? true : false;

      switch (pin)
      {
        case 1:
          digitalWrite(ledPin, state ? HIGH : LOW);
          break;
        case 2:
          digitalWrite(ledPin2, state ? HIGH : LOW);
          break;
        case 3:
          digitalWrite(ledPin3, state ? HIGH : LOW);
          break;
      }
    }

    request->send(200, "application/json", "{\"status\":\"true\",\"message\":\"Toggle Led\"}");
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {

    if ( Update.hasError() ) {
      request->send(500, "text/plain", "{\"status\":\"false\",\"message\":\"Update Failed!\"}");
    }
    else {
      request->send(200, "text/plain", "{\"status\":\"true\",\"message\":\"Update Successful! Restarting...\"}");
    }
    
    delay(1000);
    ESP.restart();
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    
    if (!index){
      Serial.printf("Starting Update: %s\n", filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { 
        Update.printError(Serial);
      }
    }
    
    if (!Update.write(data, len)) {
      Update.printError(Serial);
    }
    if (final) {
      if (Update.end(true)) { 
        Serial.printf("Update Completed: %uB\n", index + len);
      } else {
        Update.printError(Serial);
      }
    }
  });

  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  server.begin();
}


void loop() 
{
  if(!WiFi.isConnected() &&  scan_flag == false){
    if(millis() - prev_millis >10000 || prev_millis == 0){
      prev_millis = millis();
      Serial.println("Scanning Wi-Fi networks...");
      numNetworks = WiFi.scanNetworks();

      while(WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        delay(100);
      }

      if (WiFi.scanComplete() ==  WIFI_SCAN_FAILED) {
        Serial.println("Wifi scan is not completed");
      }

      networks.clear();

      for (int i = 0; i < numNetworks; i++) {
        Serial.println(WiFi.SSID(i));
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
      }

      WiFi.scanDelete();
    }
    if (!eeprom_ssid.isEmpty() && !eeprom_password.isEmpty()) {
      Serial.println("buradan başlıyorum ben babaa");
      Serial.println(eeprom_password);
      Serial.println(eeprom_ssid);
      WiFi.begin(eeprom_ssid.c_str(), eeprom_password.c_str());
      delay(3000);
      Serial.println(WiFi.status());
      Serial.println(WiFi.localIP());
      WiFi.mode(WIFI_STA);
      if(!WiFi.isConnected()){
        WiFi.disconnect();
      }
    }
  }
  sendRandomValues(); 
  ws.textAll("{\"values\":[" + String(latestRandomValues[0]) + "," + String(latestRandomValues[1]) + "," + String(latestRandomValues[2]) + "]}"); 
  delay(1000);
}