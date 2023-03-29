#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <esp_now.h>

#define RST_PIN 15
#define SDA_SS_PIN 5
#define BTN1 27
#define BTN2 4
#define BTN3 14

bool testWifi(void);
void launchWeb(void);
void setupAP(void);
void createWebServer();

int i = 0;
int statusCode, btn1PrevState, btn2PrevState, btn3PrevState;
const char *ssid = "parth";
const char *password = "1234567890";
static int people_count = 0;
String st;
String content;
String esid;
String epass = "";

typedef struct struct_message {
  int distance_1_cm;
  int distance_2_cm;
  int direction;
} struct_message;

struct_message incomingReadings;

MFRC522 mfrc522(SDA_SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer server(80);

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  // Copies the sender mac address to a string
  char macStr[18];
  Serial.print("Packet received from: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
  
  
  Serial.printf("distance 1 value (cm): %d \n", incomingReadings.distance_1_cm);
  Serial.printf("distance 2 value (cm): %d \n", incomingReadings.distance_2_cm);
  Serial.printf("direction value: %d \n", incomingReadings.direction);
  if (incomingReadings.direction==0){
    people_count ++;
  }
  else{
    people_count --;
  }
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Number of people");

  lcd.setCursor(0, 1);
  lcd.print(people_count);
  Serial.println();
}


void setup()
{
  Serial.begin(9600);
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);

  pinMode(LED_BUILTIN, OUTPUT);

  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println(F("Read personal data on a MIFARE PICC:"));
 
  lcd.init();

  lcd.backlight();
  lcd.setCursor(3, 0);
  lcd.print("Hello, world!");

  EEPROM.begin(512);
  delay(10);

  Serial.println();
  Serial.println();
  Serial.println("Startup");

  Serial.println("Reading EEPROM ssid");
  for (int i = 0; i < 32; ++i)
  {
    esid += char(EEPROM.read(i));
  }
  Serial.println();
  Serial.print("SSID: ");
  Serial.println(esid);
  Serial.println("Reading EEPROM pass");
  for (int i = 32; i < 96; ++i)
  {
    epass += char(EEPROM.read(i));
  }
  Serial.print("PASS: ");
  Serial.println(epass);

  WiFi.mode(WIFI_AP_STA);

  WiFi.begin(esid.c_str(), epass.c_str());

  if (!testWifi())
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connection Status Negative");
    lcd.setCursor(0, 1);
    lcd.print("Turning the HotSpot On");

    Serial.println("Connection Status Negative");
    Serial.println("Turning the HotSpot On");
    launchWeb();
    setupAP(); // Setup HotSpot
  }
  Serial.println();
  Serial.println("Waiting.");
  lcd.clear();
   
  while ((WiFi.status() != WL_CONNECTED))
  {
    Serial.print(".");
    lcd.setCursor(0, 0);
    lcd.print("Waiting for WiFi");
    lcd.setCursor(0, 1);
    lcd.print("credential");
    delay(100);
    server.handleClient();
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(OnDataRecv);

  delay(1000);

}

void loop()
{


  if (digitalRead(BTN1) == 0)
  {
    lcd.clear();
    lcd.print("Bad");
    delay(2000);
    lcd.clear();
  }

  if (digitalRead(BTN2) == 0)
  {
    lcd.clear();
    lcd.print("Medium");
    delay(2000);
    lcd.clear();
  }

  if (digitalRead(BTN3) == 0)
  {
    lcd.clear();
    lcd.print("Good");
    delay(2000);
    lcd.clear();
  }

  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++)
    key.keyByte[i] = 0xFF;
  byte block;
  byte len;
  MFRC522::StatusCode status;

  if (!mfrc522.PICC_IsNewCardPresent())
  {
    return;
  }

  if (!mfrc522.PICC_ReadCardSerial())
  {
    return;
  }

  Serial.println(F("**Card Detected:**"));

  for (byte i = 0; i < mfrc522.uid.size; i++)
  {
    Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : "");
    Serial.print(mfrc522.uid.uidByte[i], HEX);
    content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : ""));
    content.concat(String(mfrc522.uid.uidByte[i], HEX));
  }

  Serial.println("");
  content.toUpperCase();
  char *idTag = new char[content.length() + 1];
  strcpy(idTag, content.c_str());

  lcd.setCursor(0, 0);
  lcd.print(String(idTag));
  delay(100);
  // mfrc522.PICC_DumpToSerial(&(mfrc522.uid));      //uncomment this to see all blocks in hex

  Serial.println(F("\n**End Reading**\n"));
  content = "";
  lcd.clear();
}

uint8_t calculate_checksum(uint8_t *data) {
  uint8_t checksum = 0;
  checksum |= 0b11000000 & data[1];
  checksum |= 0b00110000 & data[2];
  checksum |= 0b00001100 & data[3];
  checksum |= 0b00000011 & data[4];
  return checksum;
}


bool testWifi(void)
{
  int c = 0;
  // Serial.println("Waiting for Wifi to connect");
  lcd.clear();
  while (c < 20)
  {

    if (WiFi.status() == WL_CONNECTED)
    {
      return true;
    }
    delay(500);
    Serial.print("*");
    c++;

    lcd.setCursor(0, 0);
    lcd.print("Waiting for WiFi");
  }
  lcd.clear();
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
  digitalWrite(LED_BUILTIN, HIGH);
  return false;
}

void createWebServer()
{
  {
    server.on("/", []()
              {
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html>Welcome to Wifi Credentials Update page";
      content += "<form action=\"/scan\" method=\"POST\"><input type=\"submit\" value=\"scan\"></form>";
      content += ipStr;
      content += "<p>";
      content += st;
      content += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32><input name='pass' length=64><input type='submit'></form>";
      content += "</html>";
      server.send(200, "text/html", content); });
    server.on("/scan", []()
              {
      //setupAP();
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html>go back";
      server.send(200, "text/html", content); });
    server.on("/setting", []()
              {
      String qsid = server.arg("ssid");
      String qpass = server.arg("pass");
      if (qsid.length() > 0 && qpass.length() > 0) {
        Serial.println("clearing eeprom");
        for (int i = 0; i < 96; ++i) {
          EEPROM.write(i, 0);
        }
        Serial.println(qsid);
        Serial.println("");
        Serial.println(qpass);
        Serial.println("");
        Serial.println("writing eeprom ssid:");
        for (int i = 0; i < qsid.length(); ++i)
        {
          EEPROM.write(i, qsid[i]);
          Serial.print("Wrote: ");
          Serial.println(qsid[i]);
        }
        Serial.println("writing eeprom pass:");
        for (int i = 0; i < qpass.length(); ++i)
        {
          EEPROM.write(32 + i, qpass[i]);
          Serial.print("Wrote: ");
          Serial.println(qpass[i]);
        }
        EEPROM.commit();
        content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
        statusCode = 200;
        ESP.restart();
      } else {
        content = "{\"Error\":\"404 not found\"}";
        statusCode = 404;
        Serial.println("Sending 404");
      }
      server.sendHeader("Access-Control-Allow-Origin", "*");
      server.send(statusCode, "application/json", content); });
  }
}

void launchWeb()
{
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED)
    Serial.println("WiFi connected");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Please Connect to AP ");
  lcd.setCursor(0, 1);
  lcd.print("GridenPower in WiFi");
  lcd.setCursor(0, 2);
  lcd.print("And go to to SoftAP IP: ");
  lcd.setCursor(0, 3);
  lcd.print(WiFi.softAPIP());
  delay(1000);
  createWebServer();
  // Start the server
  server.begin();
  Serial.println("Server started");
}

void setupAP(void)
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      // Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
  st = "<ol>";
  for (int i = 0; i < n; ++i)
  {
    // Print SSID and RSSI for each network found
    st += "<li>";
    st += WiFi.SSID(i);
    st += " (";
    st += WiFi.RSSI(i);
    st += ")";
    // st += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    st += "</li>";
  }
  st += "</ol>";
  delay(100);
  WiFi.softAP("GridenPower", "");
  Serial.println("Initializing_softap_for_wifi credentials_modification");
  launchWeb();
  Serial.println("over");
}


