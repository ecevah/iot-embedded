#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

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

int latestRandomValues[3] = {0, 0, 0}; // Üç farklı rastgele değeri saklayacak dizi

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
    sendLatestRandomValues(client); // Bağlı istemciye en son rastgele değerleri gönder
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("Client disconnected");
  }
}

void sendRandomValues() {
  // Üç farklı rastgele değer üret
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
  request->send(200, "application/json", "{\"status\":\"true\",\"message\":\"Network credentials deleted, device reset to AP+STA mode.\"}");
  delay(1000);
  WiFi.disconnect(); 
  delay(1000);
  ESP.restart();
}

void setup() 
{
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(ledPin, OUTPUT);
  pinMode(ledPin2, OUTPUT); 

  eeprom_ssid = readEEPROMString(0, 32); 
  int passwordStart = eeprom_ssid.length() + 1; 
  eeprom_password = readEEPROMString(passwordStart, 64 - passwordStart); 
  
  Serial.println(eeprom_ssid + eeprom_password);

  if (eeprom_ssid && eeprom_password) {
    WiFi.mode(WIFI_AP_STA); 
    delay(1000);
    WiFi.begin(eeprom_ssid.c_str(), eeprom_password.c_str());
    delay(3000);
    Serial.println(WiFi.status());

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi bağlantısı başarılı!");
      Serial.println(WiFi.status());
      Serial.println(WiFi.localIP());
      WiFi.mode(WIFI_STA);
      wifiConnected = true;
    }
  } else if (!wifiConnected) {
      WiFi.mode(WIFI_AP_STA);
      delay(1000);
      WiFi.softAP(ssid, password);
      Serial.println("SoftAP modunda WiFi ağı başlatıldı.");
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

    Serial.print("Bağlanılan SSID: ");
    Serial.println(ssid);
    Serial.print("Şifre: ");
    Serial.println(password);

    WiFi.begin(ssid, password);
    for(int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++){
      Serial.print(".");
      delay(100);
    }
    Serial.print(WiFi.status() == WL_CONNECTED);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Bağlandı!");
      Serial.print(WiFi.localIP().toString());
      clearEEPROM();
      writeEEPROMString(0, ssid);
      int passwordStart = strlen(ssid) + 1;
      writeEEPROMString(passwordStart, password);
      request->send(200, "application/json", "{\"status\": \"true\",\"message\": \"Baglantı Basarılı.\",\"ip\": \"" + WiFi.localIP().toString() + "\"}");
      delay(1000);
      WiFi.mode(WIFI_MODE_STA);

    } else {
      request->send(500, "application/json", "{\"error\":\"Baglantı basarısız oldu.\",\"status\":\"false\"}");
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
  sendRandomValues(); // Üç farklı rastgele değerleri üret
  ws.textAll("{\"values\":[" + String(latestRandomValues[0]) + "," + String(latestRandomValues[1]) + "," + String(latestRandomValues[2]) + "]}"); // Üç rastgele değeri gönder
  delay(1000);
}