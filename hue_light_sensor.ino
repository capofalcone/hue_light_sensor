#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NTPClient.h>

#include "passwords.h"

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 1800000);

// uncomment and fill in or create a password.h file and set there these constants
// const char* ssid     = "your_ssid";
// const char* password = "your_wifi_password";
// const String baseHueUrl = "http://ip_local_address/api/api_password";

const int sensorMove = 12;
const int sensorLight = 36;
int valueMove = 0;
int valueLight = 0;

bool lastMoveState = false;

const unsigned int restoreTime = 10 * 1000;
// Sensor from plastic bag "28"
const int lightCutOff = 1300;

const String awakeBriHigh = "254";
const String awakeBriLow = "100";
String lastAwakeSet = "";
const String awakeCt = "400";

unsigned long lastTriggerTime = 0;
int lastPinRead = 0;
bool isTriggered = false;

struct lampState {
  String isOn;
  int bri;
  int ct;
};

// 5 -> mensola
// 6 -> frigo
// 9  -> letto marco
// 10 -> letto elisa
int lampsCount = 4;
String lamps[] = {"5","6","9","10"};
lampState lastLampsState[4];
bool isChanged[] = {false,false,false,false};

bool isNowAfter(int hour, int minutes) {
  bool isAfter = false;
  if (timeClient.getHours() > hour) {
    isAfter = true;
  }
  else {
    if ((timeClient.getHours() == hour) && (timeClient.getHours() > minutes)) {
      isAfter = true;
    }
  }
  return isAfter;
}

bool isNowBetween(int hourStart, int minutesStart, int hourEnd, int minutesEnd) {
  bool a = isNowAfter(hourStart, minutesStart);
  bool b = isNowAfter(hourEnd, minutesEnd);
  return (a && !b);
}

void getCurrentstate(String id, String* isOn, int* bri, int* ct) {
    if(WiFi.isConnected()) {

        HTTPClient http;
        int httpCode = -1;
        http.begin(baseHueUrl + "/lights/" + id);
        httpCode = http.sendRequest("GET");
        // httpCode will be negative on error
        if(httpCode > 0) {
            // HTTP header has been send and Server response header has been handled
            // Serial.printf("[HTTP] GET... code: %d\n", httpCode);
            // file found at server
            if(httpCode == HTTP_CODE_OK) {
              String payload = http.getString();
              //Serial.println(payload);

              StaticJsonBuffer<1200> jsonBuffer;
              JsonObject& root = jsonBuffer.parseObject(payload);

              if (!root.success()) {
                Serial.println("parseObject() failed");
                return;
              }
              const char* state = root["state"]["on"];
              *isOn = String(state);
              *bri = root["state"]["bri"];
              *ct = root["state"]["ct"];
            }
        } else {
            Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        }

        http.end();
    }
}

void setAwake(String id) {
  bool lowPeriod1 = isNowBetween(21,30,24,0);
  bool lowPeriod2 = isNowBetween(7,0,7,45);
  if (lowPeriod1 || lowPeriod2) {
    lastAwakeSet = awakeBriLow;
  }
  else {
    lastAwakeSet = awakeBriHigh;
  }
  if(WiFi.isConnected()) {
    HTTPClient http;
    int httpCode = -1;
    http.begin(baseHueUrl + "/lights/" + id + "/state");
    httpCode = http.sendRequest("PUT", "{\"on\":true, \"bri\":" + lastAwakeSet + ",\"ct\":" + awakeCt + "}");
    http.end();
    // Wait for light transition to end before continue
    delay(400);
  }
}

void restoreFromAwake(String id, int i) {
  Serial.println("id: " + id + " i: " + i);
  if(WiFi.isConnected()) {
    HTTPClient http;
    int httpCode = -1;
    http.begin(baseHueUrl + "/lights/" + id + "/state");
    Serial.println("{\"on\":" + lastLampsState[i].isOn + ", \"bri\":" + String(lastLampsState[i].bri) + ",\"ct\":" + String(lastLampsState[i].ct) + "}");
    httpCode = http.sendRequest("PUT", "{\"on\":" + lastLampsState[i].isOn + ", \"bri\":" + String(lastLampsState[i].bri) + ",\"ct\":" + String(lastLampsState[i].ct) + "}");
    http.end();
    // Wait for light transition to end before continue
    delay(400);
  }
}

void wifiConnect() {
  pinMode(sensorMove, INPUT);
  WiFi.begin(ssid, password);
  int wait = 0;
  while ((WiFi.status() != WL_CONNECTED) && wait++ < 25) { // 25
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  timeClient.begin();
}

void setup() {
  Serial.begin(115200);
   
  wifiConnect();
    
  pinMode(sensorMove, INPUT);
}

void loop() {
  if(WiFi.isConnected()) {
    timeClient.update();
  }
  valueMove = digitalRead(sensorMove);
  valueLight = analogRead(sensorLight);

    if (valueMove == 1 && lastPinRead == 1 && isTriggered) {
      // Serial.println("(pinRead == 1 && lastPinRead == 1 && isTriggered)");
      lastTriggerTime = millis();
    }
    
    // cambio di stato da 1 a 0 e non siamo in trigger mode
    if (valueMove == 1 && lastPinRead == 0 && !isTriggered && valueLight < lightCutOff && (timeClient.getHours() >= 7 && timeClient.getHours() < 24)) {
      //Serial.println("(pinRead == 1 && lastPinRead == 0 && !isTriggered)");
      // controllare stato della lampada, salvare lo stato attuale
      // impostare il nuovo stato
      String lIsOn;
      int lBri;
      int lCt;

      for (int i=0; i < lampsCount; i++){
        getCurrentstate(lamps[i], &lIsOn, &lBri, &lCt);
        lastLampsState[i].isOn = lIsOn;
        lastLampsState[i].bri = lBri;
        lastLampsState[i].ct = lCt;
      }
      for (int i=0; i < lampsCount; i++){
        setAwake(lamps[i]);
      }

      isTriggered = true;
      lastTriggerTime = millis();
    }

    if (valueMove == 0 && (millis() - lastTriggerTime > restoreTime) && isTriggered){
      //bool isChanged[] = {false,false,false,false};

      String lIsOn;
      int lBri;
      int lCt;

      // controllo se lo stato delle lampade, confronto con lo stato richiamato dal trigger
      // se coincide, re impostare con lo stato precedente salvato, altrimenti non fare nulla
      for (int i=0; i < lampsCount; i++){
        getCurrentstate(lamps[i], &lIsOn, &lBri, &lCt);
        isChanged[i] = (lIsOn != "true") || (String(lBri) != lastAwakeSet); //|| (String(lCt) != awakeCt);
      }
      for (int i=0; i < lampsCount; i++){
        if (!isChanged[i]){
          restoreFromAwake(lamps[i], i);
        }
      }
      isTriggered = false;
    }

  
  if(!WiFi.isConnected()) {
    wifiConnect();
  }
  lastPinRead = valueMove;
  delay(25);
}
