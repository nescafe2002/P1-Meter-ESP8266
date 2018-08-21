// #include <TimeLib.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "CRC16.h"

//===Change values from here===
const char* ssid = "WIFISSID";
const char* password = "PASSWORD";
const char* hostName = "ESPP1Meter";
const char* emonCmsHost = "emoncms.org";
const int emonCmsPort = 80;
const int emonCmsNode = 8266;
const char* emonCmsApiKey = "";
const bool outputOnSerial = true;
//===Change values to here===

// Vars to store meter readings
long mEVLT = 0; //Meter reading Electrics - consumption low tariff
long mEVHT = 0; //Meter reading Electrics - consumption high tariff
long mEOLT = 0; //Meter reading Electrics - return low tariff
long mEOHT = 0; //Meter reading Electrics - return high tariff
long mEAV = 0;  //Meter reading Electrics - Actual consumption
long mEAT = 0;  //Meter reading Electrics - Actual return
long mAV1 = 0; // Actueel vermogen L1
long mAV2 = 0; // Actueel vermogen L2
long mAV3 = 0; // Actueel vermogen L3
long mAT1 = 0; // Actuele teruglevering L1
long mAT2 = 0; // Actuele teruglevering L2
long mAT3 = 0; // Actuele teruglevering L3
long mGAS = 0;    //Meter reading Gas
long prevGAS = 0;

unsigned long mNextUpdateTime = 0;
const unsigned long mUpdatePeriod = 10000;

#define MAXLINELENGTH 1023 // long enough for whole update diagram
char telegram[MAXLINELENGTH];

char checksum[6]; // 4 chars + \r\n

#define SERIAL_RX     14  // pin for SoftwareSerial RX: D5 = 14
SoftwareSerial mySerial(SERIAL_RX, -1, true, MAXLINELENGTH); // (RX, TX. inverted, buffer)

WiFiUDP g_udp;
const int g_port = 8888;

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  mySerial.begin(115200);

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(hostName);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
    mySerial.enableRx(false);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  // start UDP server
  g_udp.begin(g_port);
}


bool SendToDebug(char* sValue)
{
  IPAddress ip = WiFi.localIP();
  ip[3] = 255;

  // transmit broadcast package
  g_udp.beginPacket(ip, g_port);
  g_udp.write(sValue);
  g_udp.endPacket();
}


bool SendToEmonCms(char* sValue)
{
  HTTPClient http;
  bool retVal = false;
  char url[255];
  sprintf(url, "http://%s:%d/input/post.json?node=%d&apikey=%s&json=%s", emonCmsHost, emonCmsPort, emonCmsNode, emonCmsApiKey, sValue);
  Serial.printf("[HTTP] GET... URL: %s\n", url);
  http.begin(url); //HTTP
  int httpCode = http.GET();
  // httpCode will be negative on error
  if (httpCode > 0)
  { // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      retVal = true;
    }
  }
  else
  {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
  return retVal;
}



void UpdateGas()
{
  //sends over the gas setting to domoticz
  if(prevGAS!=mGAS)
  {
    char sValue[10];
    sprintf(sValue, "{GAS:%d}", mGAS);
    if(SendToEmonCms(sValue))
      prevGAS=mGAS;
  }
}

void UpdateElectricity()
{
  char sValue[255];
  sprintf(sValue, "{EVLT:%d,EVHT:%d,EOLT:%d,EOHT:%d,EAV:%d,EAT:%d,AV1:%d,AV2:%d,AV3:%d,AT1:%d,AT2:%d,AT3:%d}", mEVLT, mEVHT, mEOLT, mEOHT, mEAV, mEAT, mAV1, mAV2, mAV3, mAT1, mAT2, mAT3);
  SendToEmonCms(sValue);
}


bool isNumber(char* res, int len) {
  for (int i = 0; i < len; i++) {
    if (((res[i] < '0') || (res[i] > '9'))  && (res[i] != '.' && res[i] != 0)) {
      return false;
    }
  }
  return true;
}

int FindCharInArrayRev(char array[], char c, int len) {
  for (int i = len - 1; i >= 0; i--) {
    if (array[i] == c) {
      return i;
    }
  }
  return -1;
}

long getValidVal(long valNew, long valOld, long maxDiffer)
{
  //check if the incoming value is valid
      if(valOld > 0 && ((valNew - valOld > maxDiffer) && (valOld - valNew > maxDiffer)))
        return valOld;
      return valNew;
}

long getValue(char* buffer, int maxlen) {
  int s = FindCharInArrayRev(buffer, '(', maxlen - 2);
  if (s < 8) return 0;
  if (s > 32) s = 32;
  int l = FindCharInArrayRev(buffer, '*', maxlen - 2) - s - 1;
  if (l < 4) return 0;
  if (l > 12) return 0;
  char res[16];
  memset(res, 0, sizeof(res));

  if (strncpy(res, buffer + s + 1, l)) {
    if (isNumber(res, l)) {
      return (1000 * atof(res));
    }
  }
  return 0;
}

bool verifyChecksum(int len) {

  unsigned int crc = CRC16(0x0000, (unsigned char*)telegram, len);
  return (strtol(checksum, NULL, 16) == crc);
}

bool decodeTelegram(int len) {

  // Read each command pair
  char* command = strtok(telegram, "\n");
  while (command != 0) {
    int cmdlen = strlen(command);

    // 1-0:1.8.1(000992.992*kWh)
    // 1-0:1.8.1 = Elektra verbruik laag tarief (DSMR v4.0)
    if (strncmp(command, "1-0:1.8.1(", strlen("1-0:1.8.1(")) == 0)
      mEVLT = getValue(command, cmdlen);

    // 1-0:1.8.2(000560.157*kWh)
    // 1-0:1.8.2 = Elektra verbruik hoog tarief (DSMR v4.0)
    else if (strncmp(command, "1-0:1.8.2(", strlen("1-0:1.8.2(")) == 0)
      mEVHT = getValue(command, cmdlen);

    // 1-0:2.8.1(000348.890*kWh)
    // 1-0:2.8.1 = Elektra opbrengst laag tarief (DSMR v4.0)
    else if (strncmp(command, "1-0:2.8.1(", strlen("1-0:2.8.1(")) == 0)
      mEOLT = getValue(command, cmdlen);

    // 1-0:2.8.2(000859.885*kWh)
    // 1-0:2.8.2 = Elektra opbrengst hoog tarief (DSMR v4.0)
    else if (strncmp(command, "1-0:2.8.2(", strlen("1-0:2.8.2(")) == 0)
      mEOHT = getValue(command, cmdlen);

    // 1-0:1.7.0(00.424*kW) Actueel verbruik
    // 1-0:2.7.0(00.000*kW) Actuele teruglevering
    // 1-0:1.7.x = Electricity consumption actual usage (DSMR v4.0)
    else if (strncmp(command, "1-0:1.7.0(", strlen("1-0:1.7.0(")) == 0)
      mEAV = getValue(command, cmdlen);
    else if (strncmp(command, "1-0:2.7.0(", strlen("1-0:2.7.0(")) == 0)
      mEAT = getValue(command, cmdlen);

    // Actueel vermogen L1, L2, L3
    else if (strncmp(command, "1-0:21.7.0(", strlen("1-0:21.7.0(")) == 0)
      mAV1 = getValue(command, cmdlen);
    else if (strncmp(command, "1-0:41.7.0(", strlen("1-0:41.7.0(")) == 0)
      mAV2 = getValue(command, cmdlen);
    else if (strncmp(command, "1-0:61.7.0(", strlen("1-0:61.7.0(")) == 0)
      mAV3 = getValue(command, cmdlen);

    // Actuele teruglevering L1, L2, L3
    else if (strncmp(command, "1-0:22.7.0(", strlen("1-0:22.7.0(")) == 0)
      mAT1 = getValue(command, cmdlen);
    else if (strncmp(command, "1-0:42.7.0(", strlen("1-0:42.7.0(")) == 0)
      mAT2 = getValue(command, cmdlen);
    else if (strncmp(command, "1-0:62.7.0(", strlen("1-0:62.7.0(")) == 0)
      mAT3 = getValue(command, cmdlen);

    // 0-1:24.2.1(150531200000S)(00811.923*m3)
    // 0-1:24.2.1 = Gas (DSMR v4.0) on Kaifa MA105 meter
    else if (strncmp(command, "0-1:24.2.1(", strlen("0-1:24.2.1(")) == 0)
      mGAS = getValue(command, cmdlen);

    // Find the next command in input string
    command = strtok(0, "\n");
  }

  // sanity check
  return (abs(mEAV - mAV1 - mAV2 - mAV3) < 10 && abs(mEAT - mAT1 - mAT2 - mAT3) < 10);
}

void readTelegram() {
  if (mySerial.available()) {

    int len = mySerial.readBytesUntil('!', telegram, MAXLINELENGTH);
    telegram[len] = '!';
    telegram[len + 1] = 0;

    if (mySerial.available()) {

      mySerial.readBytesUntil('\n', checksum, 6);
      checksum[4] = 0;

      if (millis() > mNextUpdateTime && verifyChecksum(len + 1) && decodeTelegram(len + 1)) {
          UpdateElectricity();
          UpdateGas();
          mNextUpdateTime = millis() + mUpdatePeriod;
      }
    }
  }
}

void loop() {
  readTelegram();
  ArduinoOTA.handle();
  yield();
}
