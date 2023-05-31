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
#include <SoftwareSerial.h>

#define MYPORT_TX 21
#define MYPORT_RX 22

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

EspSoftwareSerial::UART myPort;

bool testWifi(void);
void launchWeb(void);
void setupAP(void);
void createWebServer();
void command(String cmd);
void send_feedback(int rating);
void send_footfall(int in_footfalls, int out_footfalls);
void send_cleaning_activity(int counter, char *tag);
void dataWriteTouch(int value, byte address_h, byte address_l);

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
unsigned long int last_time_scaned_rfid;
unsigned long int last_time_send_number;
bool once{false};

int add_happy_sad_ok[2] = {1, 0};

int reset_to_prev_add[2] = {11, 0};

byte happy[2] = {0x00, 0x00};
byte sad[2] = {0x00, 0x02};
byte ok[2] = {0x00, 0x01};
bool got_the_data = false;

byte read_signal;
byte tdata[200];
int count_to;
int len_of_data;
bool start_count;
long int prev_millis{};
byte address[2];

typedef struct struct_message
{
  int distance_1_cm;
  int distance_2_cm;
  int direction;
} struct_message;
struct_message incomingReadings;

union IntToBytes {
  int value;
  uint8_t bytes[8];
};

typedef struct ssid_send
{
  String ssid;
} ssid_send;
ssid_send SsidSend;

MFRC522 mfrc522(SDA_SS_PIN, RST_PIN);
// LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer server(80);

// A0:76:4E:1A:92:98
uint8_t broadcastAddress[] = {0xA0, 0x76, 0x4E, 0x1A, 0x92, 0x98};

volatile bool send_the_data = false;
volatile bool new_data_available {false};
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len)
{

  char macStr[18];
  debug("Packet received from: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));

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

  new_data_available = true;
  
  if (in_people_count - last_in_footdall_counts > MAX_IN_COUNT)
  {
    last_in_footdall_counts = in_people_count;
    send_the_data = true;
  }
}

// 5a a5 07 82 00 84 5a 01 00 00

void dataWriteTouch(int value, byte address_h, byte address_l)
{

  IntToBytes data;
  data.value = value;
  
  Serial.println();
  Serial.println(data.value);
  Serial.print(data.bytes[0], HEX);
  Serial.print(" ");
  Serial.print(data.bytes[1], HEX);
  Serial.print(" ");
  Serial.print(data.bytes[2], HEX);
  Serial.print(" ");
  Serial.print(data.bytes[3], HEX);
  Serial.print(" ");
  Serial.print(data.bytes[4], HEX);
  Serial.print(" ");
  Serial.print(data.bytes[5], HEX);
  Serial.print(" ");
  Serial.print(data.bytes[6], HEX);
  Serial.print(" ");
  Serial.print(data.bytes[7], HEX);
  Serial.print(" ");
  Serial.println();

  byte array[14] = {0x5a, 0xa5, 0x0b, 0x82, address_h, address_l, data.bytes[7], data.bytes[6], data.bytes[5], data.bytes[4], data.bytes[3], data.bytes[2], data.bytes[1], data.bytes[0]};
  myPort.write(array, 14);

  // byte hex[8];
  // byte * data = reinterpret_cast<byte*> (&value);
  // memcpy(hex, data, 8);
  // byte array[14] = { 0x5a, 0xa5, 0x0b, 0x82, address_h, address_l, hex[7], hex[6], hex[5], hex[4], hex[3], hex[2], hex[1], hex[0]};

  //   myPort.write(array, 14);
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

  myPort.begin(115200, SWSERIAL_8N1, MYPORT_RX, MYPORT_TX, false);
  if (!myPort)
  { // If the object did not initialize, then its configuration is invalid
    Serial.println("Invalid EspSoftwareSerial pin configuration, check config");
    while (1)
    { // Don't continue with invalid configuration
      delay(1000);
    }
  }
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);

  pinMode(LED_BUILTIN, OUTPUT);

  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println(F("Read personal data on a MIFARE PICC:"));

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
  delay(1000);

  dataWriteTouch(0, 0x14, 0x00);
  dataWriteTouch(0, 0x15, 0x00);
  dataWriteTouch(0, 0x16, 0x00);
}

void loop()
{

  while (myPort.available())
  {
    read_signal = myPort.read();
    // Serial.print(read_signal, HEX);
    // Serial.print(" ");
    if (read_signal == 0x5a)
    {
      start_count = true;
    }
    if (start_count == true)
    {
      count_to++;
    }

    if (count_to == 3)
    {
      len_of_data = read_signal; // 05
    }

    else if (count_to > 4)
    {
      tdata[len_of_data - 3];
      for (int i = 0; i < len_of_data - 2; i++)
      {

        if (i == 0)
        {

          while (!myPort.available())
            ;
          address[1] = (byte)myPort.read();
        }
        else if (i == 1)
        {
          while (!myPort.available())
            ;
          address[0] = (byte)myPort.read();
        }
        else if (i > 1)
        {
          while (!myPort.available())
            ;
          tdata[i - 2] = myPort.read();
        }
      }
      start_count = false;
      count_to = 0;

      // Serial.print("Address: ");
      // for (int i = 0; i < 2; i++)
      // {
      //   Serial.print(address[i], HEX);
      //   Serial.print(" ");
      // }
      // Serial.println();

      // Serial.print("Data: ");
      // for (int i = 0; i < len_of_data - 3 - 1; i++)
      // {
      //   Serial.print(tdata[i], HEX);
      //   Serial.print(" ");
      // }
      // Serial.println();
      // Serial.println("Got the data");

      got_the_data = true;
    }
  }

  if (send_the_data == true)
  {
    send_footfall(in_people_count, out_people_count);
    send_the_data = false;
  }

  if (!memcmp(tdata, sad, sizeof(ok)) && !memcmp(address, add_happy_sad_ok, sizeof(ok)) && got_the_data)
  {
    Serial.println("BAd");
    delay(2000);
    send_feedback(0);
  }

  if (!memcmp(tdata, ok, sizeof(ok)) && !memcmp(address, add_happy_sad_ok, sizeof(ok)) && got_the_data)
  {

    Serial.println("Medium");
    delay(2000);
    send_feedback(1);
  }

  if (!memcmp(tdata, happy, sizeof(ok)) && !memcmp(address, add_happy_sad_ok, sizeof(ok)) && got_the_data)
  {
    Serial.println("Good");
    delay(2000);
    send_feedback(2);
  }

  memset(tdata, 0, sizeof tdata);
  memset(address, 0, sizeof address);

  got_the_data = false;
  // mfrc522.PICC_DumpToSerial(&(mfrc522.uid));      //uncomment this to see all blocks in hex
  if (new_data_available == true)
  {
    Serial.println("heya");
    dataWriteTouch(in_people_count, 0x14, 0x00);
    dataWriteTouch(abs(in_people_count - out_people_count), 0x15, 0x00);
    dataWriteTouch(out_people_count, 0x16, 0x00);
    last_time_send_number = millis();
    new_data_available = false;
  }
  if (millis() - last_time_scaned_rfid > 60000)
  {
    if (once == false)
    {
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

  while (c < 20)
  {

    if (WiFi.status() == WL_CONNECTED)
    {
      return true;
    }
    delay(500);
    debug("*");
    c++;
  }

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
  delay(1000);
  createWebServer();

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
