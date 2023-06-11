/*
Meisterprüfung LORA Gateway, 
Dieser Code empfängt Daten via LORA 868MHz und wertet diese aus und stellt Sie dem WEB Server zur Verfügung, 
Der Code ermöglicht eine Verbindung des ESP32 µController ​mit Wi-Fi,
V1.5, designed by Stefan Siewert
*/

// Import Wi-Fi library
#include <WiFi.h>
#include "ESPAsyncWebServer.h"

#include <SPIFFS.h>

//Libraries for LoRa
#include <SPI.h>
#include <LoRa.h>

// Libraries to get time from NTP Server
#include <NTPClient.h>
#include <WiFiUdp.h>

//Libraries for OLED Display
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//define the pins used by the LoRa transceiver module
#define SCK 18
#define MISO 19
#define MOSI 23
#define NSS 5
#define RST 14
#define DIO0 2

//433E6 for Asia
//866E6 for Europe
//915E6 for North America
#define BAND 866E6

//OLED definition
#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_WIDTH 128 //OLED display width, in pixels
#define SCREEN_HEIGHT 64 //OLED display height, in pixel
#define OLED_RESET -1 //Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D //See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

// Replace with your network credentials
const char* ssid     = "XXX";
const char* password = "XXX";

IPAddress staticIP(192, 168, 1, 15);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 192);
IPAddress dns(192, 168, 1, 1);

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Variables to save date and time
String formattedDate;
String day;
String hour;
String timestamp;

// Initialize variables to get and save LoRa data
int rssi;
String loRaMessage;
String tempC;
String current;
String voltage;
String readingID;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Replaces placeholder with DHT values
String processor(const String& var){
  //Serial.println(var);
  if(var == "TEMPERATURE"){
    return tempC;
  }
  else if(var == "AMPERE"){
    return current;
  }
  else if(var == "VOLT"){
    return voltage;
  }
  else if(var == "TIMESTAMP"){
    return timestamp;
  }
  else if (var == "RRSI"){
    return String(rssi);
  }
  return String();
}

//Initialize OLED display
void startOLED(){
  //reset OLED display via software
  pinMode(OLED_RESET, OUTPUT);
  digitalWrite(OLED_RESET, LOW);
  delay(20);
  digitalWrite(OLED_RESET, HIGH);

  //initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("LORA Gateway");
  display.display();
}

//Initialize LoRa module
void startLoRA(){
  int counter;
  //SPI LoRa pins
  SPI.begin(SCK, MISO, MOSI, SS);
  //setup LoRa transceiver module
  LoRa.setPins(SS, RST, DIO0);

  //Set LNA Gain for better RX sensitivity, by default AGC (Automatic Gain Control) is used and LNA gain is not used.
  //Supported values are between 0 and 6. If gain is 0, AGC will be enabled and LNA gain will not be used. 
  //Else if gain is from 1 to 6, AGC will be disabled and LNA gain will be used.
  LoRa.setGain(6);

  // set output power
  LoRa.setTxPower(20);

  // Change sync word (0X58) to match the receiver
  // The sync word assures you don't get LoRa messages from other LoRa transceivers
  // ranges from 0-0xFF
  LoRa.setSyncWord(0X58);

  while (!LoRa.begin(BAND) && counter < 10) {
    Serial.print(".");
    counter++;
    delay(500);
  }
  if (counter == 10) {
    // Increment readingID on every new reading
    Serial.println("Starting LoRa failed!"); 
    display.setCursor(0,10);
    display.print("Starting LoRa failed!");
    display.display();
  }
  Serial.println("LoRa Initialization OK!");
  display.setCursor(0,10);
  display.print("LoRa Initializing OK!");
  display.display();
  delay(2000);
}

void connectWiFi(){
  // Connect to Wi-Fi network with SSID and password
  if (WiFi.config(staticIP, gateway, subnet, dns, dns) == false) {
    Serial.println("Configuration failed.");
  }

  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("Connecting...\n\n");
  }

  // Print local IP address and start web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  display.setCursor(0,20);
  display.print("Access web server at: ");
  display.setCursor(0,30);
  display.print(WiFi.localIP());
  display.display();
}

// Read LoRa packet and get the sensor readings
void getLoRaData() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.print("Access web server at: ");
  display.setCursor(0,10);
  display.print(WiFi.localIP());
  Serial.print("Lora packet received: ");
  display.setCursor(0,20);
  display.print("Lora packet received: ");
  // Read packet
  while (LoRa.available()) {
    String LoRaData = LoRa.readString();
    // LoRaData format: readingID/voltage&current#temperature
    // String example: 1/27.43&654#95.34
    Serial.print(LoRaData);
    display.setCursor(0,30);
    display.print(LoRaData);
    
    // Get readingID, voltage and current
    int pos1 = LoRaData.indexOf('/');
    int pos2 = LoRaData.indexOf('&');
    int pos3 = LoRaData.indexOf('#');
    readingID = LoRaData.substring(0, pos1);
    voltage = LoRaData.substring(pos1 +1, pos2);
    current = LoRaData.substring(pos2+1, pos3);
    tempC = LoRaData.substring(pos3+1, LoRaData.length());    
  }
  // Get RSSI
  rssi = LoRa.packetRssi();
  Serial.print(" with RSSI ");    
  Serial.println(rssi);
  display.setCursor(0,40);
  display.print("with RSSI ");
  display.setCursor(64,40);
  display.print(rssi);
  display.display();
}

// Function to get date and time from NTPClient
void getTimeStamp() {
  while(!timeClient.update()) {
    timeClient.forceUpdate();
  }
  // The formattedDate comes with the following format:
  // 2018-05-28T16:00:13Z
  // We need to extract date and time
  formattedDate = timeClient.getFormattedDate();
  Serial.println(formattedDate);

  // Extract date
  int splitT = formattedDate.indexOf("T");
  day = formattedDate.substring(0, splitT);
  Serial.println(day);
  // Extract time
  hour = formattedDate.substring(splitT+1, formattedDate.length()-1);
  Serial.println(hour);
  timestamp = day + " " + hour;
  display.setCursor(0,50);
  display.println(day);
  display.setCursor(64,50);
  display.println(hour);
  display.display();
  display.clearDisplay();
}

void setup() { 
  // Initialize Serial Monitor
  Serial.begin(9600);
  startOLED();
  startLoRA();
  connectWiFi();
  
  if(!SPIFFS.begin()){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/voltage", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", voltage.c_str());
  });
  server.on("/current", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", current.c_str());
  });
    server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", tempC.c_str());
  });
  server.on("/timestamp", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", timestamp.c_str());
  });
  server.on("/rssi", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/plain", String(rssi).c_str());
//  });
//  server.on("/winter", HTTP_GET, [](AsyncWebServerRequest *request){
//    request->send(SPIFFS, "/winter.jpg", "image/jpg");
  });
  // Start server
  server.begin();
  
  // Initialize a NTPClient to get time
  timeClient.begin();
  // Set offset time in seconds to adjust for your timezone:
  // GMT +1 = 3600 (Winterzeit)
  // GMT +2 = 7200 (Sommerzeit)
  // GMT 0 = 0
  timeClient.setTimeOffset(7200);
}

void loop() {
  // Check if there are LoRa packets available
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    getLoRaData();
    getTimeStamp();
  }
}
