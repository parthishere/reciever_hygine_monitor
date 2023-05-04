#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <esp_now.h>
#include <Arduino_JSON.h>

#define PRINT_STATEMENTS

#if defined(PRINT_STATEMENTS)
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#define debugf(x) Serial.printf(x)
#else
#define debug(...)
#define debugln(...)
#define debugf(...)
#endif

#define RST_PIN 15
#define SDA_SS_PIN 5
#define BTN1 4
#define BTN2 14
#define BTN3 27

#define MAX_IN_COUNT 10
#define MONITOR_NUMBER 1100122

bool testWifi(void);
void launchWeb(void);
void setupAP(void);
void createWebServer();
void command(String cmd);
void send_feedback(int rating);
void send_footfall(int in_footfalls, int out_footfalls);
void send_cleaning_activity(int counter, char *tag);

int i = 0;
int statusCode, btn1PrevState, btn2PrevState, btn3PrevState;
const char *ssid = "parth";
const char *password = "1234567890";
static int people_count = 0, in_people_count{}, out_people_count, last_in_footdall_counts, last_out_fotdall_counts;
String st;
String content;
String esid;
String epass = "";
int last_millis{-5000};
int last_time_gps{};
int count{0};
const char *AuthenticatedTag = "E3E7719B";

typedef struct struct_message
{
  int distance_1_cm;
  int distance_2_cm;
  int direction;
} struct_message;
struct_message incomingReadings;

typedef struct ssid_send
{
  String ssid;
} ssid_send;
ssid_send SsidSend;

MFRC522 mfrc522(SDA_SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer server(80);

// A0:76:4E:1A:92:98
uint8_t broadcastAddress[] = {0xA0, 0x76, 0x4E, 0x1A, 0x92, 0x98};

volatile bool send_the_data = false;
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len)
{
  // Copies the sender mac address to a string
  char macStr[18];
  debug("Packet received from: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));

  // debugf("distance 1 value (cm): %d \n", incomingReadings.distance_1_cm);
  // debugf("distance 2 value (cm): %d \n", incomingReadings.distance_2_cm);
  debug("direction value: "); 
  debugln(incomingReadings.direction);


  if (incomingReadings.direction == 0)
  {
    people_count++;
    in_people_count++;
  }
  else
  {
    people_count--;
    out_people_count++;
  }

  lcd.clear();

  lcd.setCursor(0, 2);
  lcd.print("Out : ");
  lcd.print(out_people_count);

  lcd.setCursor(0, 1);
  lcd.print("In : ");
  lcd.print(in_people_count);

  lcd.setCursor(0, 0);
  lcd.print("Overall Number: ");
  lcd.print(people_count);
  Serial.println();
  if (in_people_count - last_in_footdall_counts > MAX_IN_COUNT)
  {
    last_in_footdall_counts = in_people_count;
    send_the_data = true;
    
  }
}

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  debug("\r\nLast Packet Send Status:\t");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
}

void setup()
{
  Serial.begin(9600);
  Serial2.begin(115200);
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
  debug("SSID: ");
  Serial.println(esid);
  Serial.println("Reading EEPROM pass");
  for (int i = 32; i < 96; ++i)
  {
    epass += char(EEPROM.read(i));
  }
  debug("PASS: ");
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

  while ((WiFi.status() != WL_CONNECTED))
  {
    debug(".");
    delay(10);
    server.handleClient();
  }

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed to add peer");
    return;
  }

  esp_now_send(broadcastAddress, (uint8_t *)&SsidSend, sizeof(SsidSend));
  lcd.clear();
  delay(1000);

  // command("AT+CFUN=1,1");
  // delay(10000);

  // command("ATI");
  // command("AT+CMEE=1");
  // // GPRS connect
  // command("AT+NETAPN=\"airtelgprs.com\",\"\",\"\"");

  // command("AT+CGDCONT=1,\"IP\",\"airtelgprs.com\"");
  // command("AT+XIIC=1");
  // command("AT+CGATT=1");

  // command("AT+NWCHANNEL=1");
  // command("AT+CGACT=1,1");

  // command("AT+HTTPSPARA=url,feedback-247.com/api/hygiene/save_footfall");
  // command("AT+HTTPSPARA=port,443");

  // command("AT+HTTPSSETUP");
  // command("AT+HTTPSACTION=0");
  // command("AT+HTTPSCLOSE");
  // command("+HTTPSRECV");

  // command("AT+NETSHAREMODE=1");
  // command("AT+NETSHAREACT=2,1,0,airtelgprs.com,card,card,0");
}
unsigned long int last_time_scaned_rfid;
bool once{false};
void loop()
{
  if (send_the_data == true){
    send_footfall(in_people_count, out_people_count);
    send_the_data = false;
    lcd.clear();

    lcd.setCursor(0, 2);
    lcd.print("Data Sent ");
    delay(1000);
    lcd.clear();
  myPort.begin(115200, SWSERIAL_8N1, MYPORT_RX, MYPORT_TX, false);
  if (!myPort) {  // If the object did not initialize, then its configuration is invalid
    Serial.println("Invalid EspSoftwareSerial pin configuration, check config");
    while (1) {  // Don't continue with invalid configuration
      delay(1000);
    }
  }
  }
  if (digitalRead(BTN1) == 0)
  {
    lcd.clear();
    lcd.print("Bad");
    delay(2000);
    lcd.clear();
    send_feedback(0);
  }

  if (digitalRead(BTN2) == 0)
  {
    lcd.clear();
    lcd.print("Medium");
    delay(2000);
    lcd.clear();
    send_feedback(1);
  }

  if (digitalRead(BTN3) == 0)
  {
    lcd.clear();
    lcd.print("Good");
    delay(2000);
    lcd.clear();
    send_feedback(2);
  }

  // mfrc522.PICC_DumpToSerial(&(mfrc522.uid));      //uncomment this to see all blocks in hex
  if (millis() - last_time_scaned_rfid > 60000)
  {
    if (once == false){
      lcd.clear();
      once = true;
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
      // debug(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : "");
      // debug(mfrc522.uid.uidByte[i], HEX);
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
    if (strcmp(idTag, AuthenticatedTag) == 0)
    {
      // same tag
      //  we have to set logic for setting up the senind of id tag
      debugln("yes inside the authentication");
      last_time_scaned_rfid = millis();
      send_cleaning_activity(0, idTag);
      once = false;
    }
    Serial.println(F("\n**End Reading**\n"));
    content = "";
    lcd.clear();
    
  }

  
}

uint8_t calculate_checksum(uint8_t *data)
{
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
    debug("*");
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
          debug("Wrote: ");
          Serial.println(qsid[i]);
        }
        Serial.println("writing eeprom pass:");
        for (int i = 0; i < qpass.length(); ++i)
        {
          EEPROM.write(32 + i, qpass[i]);
          debug("Wrote: ");
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
  debug("Local IP: ");
  Serial.println(WiFi.localIP());
  debug("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connect to AP ");
  lcd.setCursor(0, 1);
  lcd.print("GridenPower in WiFi");
  lcd.setCursor(0, 2);
  lcd.print("IP(open in chrome): ");
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
    debug(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      debug(i + 1);
      debug(": ");
      debug(WiFi.SSID(i));
      debug(" (");
      debug(WiFi.RSSI(i));
      debug(")");
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

String serverNameforSaveFeedback = "https://feedback-247.com/api/hygiene/save_feedback";

void send_feedback(int rating)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFiClientSecure client;
    // HTTPClient http;
    client.setInsecure();

    debug("[HTTP] begin...\n");
    // configure traged server and url
    // http.begin("https://www.howsmyssl.com/a/check", ca); //HTTPS
    String data = "monitor_no=" + String(MONITOR_NUMBER) + "&rating=" + rating;

    if (client.connect("feedback-247.com", 443))
    {
      client.println("POST /api/hygiene/save_feedback HTTP/1.1");
      client.println("Host: feedback-247.com");
      client.println("User-Agent: ESP32");
      client.println("Authorization: Bearer 6oYYocRGUQ1Qc33s2jfOlCLDeBFO7i4Yist4KqI1GRGRuKczlH");
      client.println("Content-Type: application/x-www-form-urlencoded;");
      client.println("Content-Length: " + String(data.length()));
      client.println();
      client.println(data);
      Serial.println(F("Data were sent successfully"));
      while (client.available() == 0)
        ;
      String c{};
      while (client.available())
      {
        c += (char)client.read();
      }

      Serial.println(c);
      JSONVar myObject = JSON.parse(c);
      if (JSON.typeof(myObject) == "undefined")
      {
        Serial.println("Parsing input failed!");
      }

      debug("JSON object = ");
      Serial.println(myObject);

      // myObject.keys() can be used to get an array of all the keys in the object
      JSONVar keys = myObject.keys();

      for (int i = 0; i < keys.length(); i++)
      {
        JSONVar value = myObject[keys[i]];
        debug(keys[i]);
        debug(" = ");
        Serial.println(value);
      }
    }

    else
    {
      Serial.println(F("Connection wasnt established"));
    }
    Serial.println("we got the responnse");
    client.stop();
  }
}

void send_footfall(int in_footfalls, int out_footfalls)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFiClientSecure client;
    // HTTPClient http;
    delay(100);
    client.setInsecure();

    debug("[HTTP] begin...\n");
    // configure traged server and url
    // http.begin("https://www.howsmyssl.com/a/check", ca); //HTTPS
    String data = "monitor_no=" + String(MONITOR_NUMBER) + "&in_footfalls=" + in_footfalls + "&out_footfalls=" + out_footfalls;
    delay(100);

    if (client.connect("feedback-247.com", 443))
    {
      client.println("POST /api/hygiene/save_footfall HTTP/1.1");
      client.println("Host: feedback-247.com");
      client.println("User-Agent: ESP32");
      client.println("Authorization: Bearer 6oYYocRGUQ1Qc33s2jfOlCLDeBFO7i4Yist4KqI1GRGRuKczlH");
      client.println("Content-Type: application/x-www-form-urlencoded;");
      client.println("Content-Length: " + String(data.length()));
      client.println();
      client.println(data);
      Serial.println(F("Data were sent successfully"));
      while (client.available() == 0)
        ;
      while (client.available())
      {
        char c = client.read();
        Serial.write(c);
      }
    }

    else
    {
      Serial.println(F("Connection wasnt established"));
    }
    Serial.println("we got the responnse");
    client.stop();
  }
}

void send_cleaning_activity(int counter, char *tag)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    WiFiClientSecure client;
    // HTTPClient http;
    client.setInsecure();

    debug("[HTTP] begin...\n");
    // configure traged server and url
    // http.begin("https://www.howsmyssl.com/a/check", ca); //HTTPS
    String data = "monitor_no=" + String(MONITOR_NUMBER) + "&counter=" + counter + "&RFID=" + String(tag);

    if (client.connect("feedback-247.com", 443))
    {
      client.println("POST /api/hygiene/save_cleaning_activity HTTP/1.1");
      client.println("Host: feedback-247.com");
      client.println("User-Agent: ESP32");
      client.println("Authorization: Bearer 6oYYocRGUQ1Qc33s2jfOlCLDeBFO7i4Yist4KqI1GRGRuKczlH");
      client.println("Content-Type: application/x-www-form-urlencoded;");
      client.println("Content-Length: " + String(data.length()));
      client.println();
      client.println(data);
      Serial.println(F("Data were sent successfully"));
      while (client.available() == 0)
        ;
      while (client.available())
      {
        char c = client.read();
        Serial.write(c);
      }
    }

    else
    {
      Serial.println(F("Connection wasnt established"));
    }
    Serial.println("we got the responnse");
    client.stop();
  }
}

void command(String cmd)
{
  debug("Sent Command: ");
  Serial.println(cmd);
  Serial2.println(cmd);
  Serial2.write(0x0d);

  while (!Serial2.available())
  {
    if (millis() - last_millis > 1000)
    {
      if (count > 15)
      {
        count = 0;
        return;
      }
      else
      {
        last_millis = millis();
        count++;
      }
    }
  }
  Serial.println("Recieved Data: ");

  while (Serial2.available())
  {
    String resp = Serial2.readString();
    Serial.println(resp);
  }
  Serial.println();
}

void getMessage()
{
  command("AT+CMGL=\"ALL\"");
}
