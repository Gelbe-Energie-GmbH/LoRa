/*
Meisterprüfung LoRa PV Monitoring, dieser Code wertet Daten aus, die die PCB Beschaltung zur Verfügung stellt, 
Die Daten werden auf einem OLED ausgegeben und via LORA 868MHz gesendet, 
V3.0, designed by Stefan Siewert
*/

//Libraries for LoRa
#include <SPI.h>
#include <LoRa.h>

//Libraries for OLED Display
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//Libraries for DS18B20
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Wire.h>

//Libraries for calculation
#include <math.h>

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

//DS18B20 definition
#define TEMP_SENSE_PIN 4
float tempC=0; //Temperature in Celcius
//float tempF = 0; //Temperature in Farenheit
const int oneWireBus = 2; //GPIO for DS18B20
OneWire oneWire(TEMP_SENSE_PIN); //Setup a oneWire instance to communicate with any OneWire devices
DallasTemperature sensors(&oneWire);

//Voltage and Current definition and GPIO
const int R1 = 470000;   // Resistance of R1 in Ohms
const int R2 = 56000;    // Resistance of R2 in Ohms
const int R4 = 1000;    // Resistance of R4 in Ohms
const int R5 = 2000;   // Resistance of R5 in Ohms
const int VOLTAGE_PIN = 34;
const int CURRENT_PIN = 35;
const float SENSITIVITY = 0.200; // 200mV/A for ACS724LLCTR-20AU-T, 200mV/A for CT415-HSN820DR, 185mV/A for ACS712ELCTR-05B-T
double ACSoffset = 0.500; // Zero Current offset for ACS724LLCTR-20AU-T, CT415-HSN820DR and ACS712ELCTR-05B-T 500mV
const int NUM_READINGS = 1000;

double power = 0 ; //Power in Watt
double energy = 0 ; //Energy in Watt-Hour

//packet counter
int readingID = 0;
int counter = 0;

String LoRaMessage = "";

unsigned long last_time = 0;
unsigned long current_time = 0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//Initialize OLED display
void startOLED() {
  //reset OLED display via software
  pinMode(OLED_RESET, OUTPUT);
  digitalWrite(OLED_RESET, LOW);
  delay(20);
  digitalWrite(OLED_RESET, HIGH);

  //initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); //Don't proceed, loop forever
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("LORA SENDER");
  display.display();
}

//Initialize LoRa module
void startLoRA() {
  //SPI LoRa pins
  Serial.println("LoRa Initialization started!");
  display.setCursor(0, 10);
  display.println("LoRa Initialization started!");
  SPI.begin(SCK, MISO, MOSI, NSS);
  //setup LoRa transceiver module
  LoRa.setPins(NSS, RST, DIO0);

  //Change sync word (0X58) to match the receiver
  //The sync word assures you don't get LoRa messages from other LoRa transceivers
  //ranges from 0-0xFF
  LoRa.setSyncWord(0X58);

  // set output power, default 17 dBm
  LoRa.setTxPower(14);

  while (!LoRa.begin(BAND) && counter < 10) {
    Serial.print(".");
    counter++;
    delay(500);
    Serial.print("Set Freq to: "); Serial.println(BAND);
  }
  if (counter == 10) {
    //Increment readingID on every new reading
    readingID++;
    Serial.println("Starting LoRa failed!");
    display.setCursor(0, 30);
    display.print("Starting LoRa failed!");
    display.display();
  }

  Serial.println("LoRa Initialization OK!");
  display.setCursor(0, 40);
  display.print("LoRa Initializing OK!");
  display.display();
}

//Function to Calculate Solar Panel Voltage
double getVoltage() {
  double voltage = 0;
  double reading = analogRead(VOLTAGE_PIN); // Reference voltage is 3v3 so maximum reading is 3v3 = 4095 in range 0 to 4095
  if(reading < 1 || reading > 4095) return 0;
  //return -0.000000000009824 * pow(reading,3) + 0.000000016557283 * pow(reading,2) + 0.000854596860691 * reading + 0.065440348345433;
  return -0.000000000000016 * pow(reading,4) + 0.000000000118171 * pow(reading,3)- 0.000000301211691 * pow(reading,2)+ 0.001109019271794 * reading + 0.034143524634089;
}

//Function to Calculate Solar Panel Current
double getCurrent() {
  int sensorValue = 0;

  for (int i = 0; i < NUM_READINGS; i++) {
    sensorValue += analogRead(CURRENT_PIN);
  }

  double voltage = (sensorValue / (double)NUM_READINGS) * 3.3 / 2047.0;
  double vout = voltage * (R4 + R5) / R5;
  double current = abs((vout - ACSoffset) / SENSITIVITY);

  return current;
}

void readTemp(){
  //read temperature from DS18B20
  sensors.requestTemperatures(); //get temperatures
  tempC = sensors.getTempCByIndex(0);
  //tempF = sensors.getTempFByIndex(0); 
}

void sendReadings() {
  double voltage = getVoltage() * (R1 + R2) / R2;
  double current = getCurrent();

  //Calculate power and energy
  power = current * voltage ; //calculate power in Watt
  last_time = current_time;
  current_time = millis();
  energy = energy +  power * (( current_time - last_time) / 3600000.0) ; //calculate power in Watt-Hour //1 Hour = 60mins x 60 Secs x 1000 Milli Secs
  
  //Send LoRa packet to receiver
  LoRaMessage = String(readingID) + "/" + String(voltage) + "&" + String(current) + "#" + String(tempC);
  LoRa.beginPacket();
  LoRa.print(LoRaMessage);
  LoRa.endPacket();

  //Display Data on OLED Display
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("LoRa packet sent!");

  //Display Temperature
  display.setCursor(0, 8);
  display.print(tempC, 1);
  display.print(" C");

  //Display Voltage
  display.setCursor(0, 16);
  display.print(voltage, 2);
  display.print(" V");

  //Display Current
  display.setCursor(0, 24);
  if (current > 0 && current < 1 )
  {
    display.print(current * 1000, 0);
    display.print(" mA");
  }
  else
  {
    display.print(current, 2);
    display.print(" A");
  }

  //Display Solar Panel Power in Watt
  display.setCursor(0, 32);
  display.print(power);
  display.print(" W");

  //Display Energy Generated by the Solar Panel
  display.setCursor(0, 40);
  if ( energy >= 1000 )
  {
    display.print(energy / 1000, 3);
    display.print(" kWh");
  }
  else
  {
    display.print(energy, 1);
    display.print(" Wh");
  }

  //Display Reading ID
  display.setCursor(0, 48);
  display.print("Reading ID:");
  display.setCursor(66, 48);
  display.print(readingID);
  display.display();
  display.clearDisplay();

  //Write Data to Serial Monitor
  Serial.print("Sending packet: ");
  Serial.println(readingID);
  Serial.print("Temperature: ");
  Serial.println(tempC);
  Serial.print("Volt: ");
  Serial.println(voltage);
  Serial.print("Ampere: ");
  Serial.println(current);
  Serial.println(analogRead(VOLTAGE_PIN));

  readingID++;
}

void setup() {
  //initialize Serial Monitor
  Serial.begin(9600);
  sensors.begin();
  startOLED();
  startLoRA();
}

void loop() {
  sendReadings();
  getVoltage();
  getCurrent();
  readTemp();
  delay(10000);
}
