#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <HardwareSerial.h>
#include <DHT.h>

// ==========================================
//              SECTION: RELAY & PUMP
// ==========================================
const int sensorPin = 35; // Moisture sensor for Relay Logic
const int relayPin = 18;

int AirValue = 3200;   
int WaterValue = 1200; 

int pumpOnThreshold = 30;  
int pumpOffThreshold = 50; 
bool isPumpRunning = false;

// ==========================================
//           SECTION: SENSORS & OLED
// ==========================================

// ---------- OLED setup ----------
#define SCREEN_ADDRESS 0x3C
#define SDA_PIN 16
#define SCL_PIN 17
Adafruit_SH1106G display(128, 64, &Wire, -1);

// ---------- RS485 (NPK) pins ----------
#define RXD2 25     // RO
#define TXD2 26     // DI
#define DE_PIN 27   // Driver Enable (HIGH = transmit)
#define RE_PIN 33   // Receiver Enable (LOW = receive)
HardwareSerial rs485(2);
byte npkRequest[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x03, 0x05, 0xCB};
float N = 0, P = 0, K = 0;

// ---------- DHT & Other Sensors ----------
#define DHTPIN 4        
#define DHTTYPE DHT22
const int moisturePin = 35; // Moisture sensor for Display Logic (Same as sensorPin)
const int phPin = 34;       
DHT dht(DHTPIN, DHTTYPE);

// ---------- pH Calibration ----------
float neutralVoltage = 1.0;  
float slope = -5.7;          

void setup() {
  Serial.begin(115200);

  // --- SETUP: RELAY (From Code 1) ---
  pinMode(relayPin, OUTPUT);
  // Active Low রিলের জন্য শুরুতে HIGH মানে পাম্প বন্ধ থাকবে
  digitalWrite(relayPin, HIGH); 
  Serial.println("--- System Initialized (Active Low Logic) ---");

  // --- SETUP: SENSORS (From Code 2) ---
  // NPK pins
  pinMode(DE_PIN, OUTPUT);
  pinMode(RE_PIN, OUTPUT);
  digitalWrite(DE_PIN, LOW);  // Start in receive mode
  digitalWrite(RE_PIN, LOW);
  rs485.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // DHT & OLED
  pinMode(DHTPIN, INPUT_PULLUP);
  Wire.begin(SDA_PIN, SCL_PIN);
  dht.begin();

  if(!display.begin(SCREEN_ADDRESS, true)){
    Serial.println("OLED init failed");
    while(1);
  }
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1); // smallest font
  display.setCursor(0,0);
  display.println("Initializing...");
  display.display();
  delay(2000);

  Serial.println("Sensors Started...");
}

void loop() {
  // ==========================================
  //        PART 1: RELAY LOGIC (Code 1)
  // ==========================================
  int rawValue = analogRead(sensorPin);
  int moisturePercent = map(rawValue, AirValue, WaterValue, 0, 100);
  moisturePercent = constrain(moisturePercent, 0, 100);

  // লজিক (Active Low):
  if (moisturePercent < pumpOnThreshold && !isPumpRunning) {
    digitalWrite(relayPin, LOW); // LOW দিলে পাম্প চালু হবে (Active Low)
    isPumpRunning = true;
    Serial.println(" -> PUMP STATUS: [ON]");
  } 
  else if (moisturePercent >= pumpOffThreshold && isPumpRunning) {
    digitalWrite(relayPin, HIGH); // HIGH দিলে পাম্প বন্ধ হবে
    isPumpRunning = false;
    Serial.println(" -> PUMP STATUS: [OFF]");
  }

  // Code 1 Serial Prints
  Serial.print("Raw: ");
  Serial.print(rawValue);
  Serial.print(" | Moisture: ");
  Serial.print(moisturePercent);
  Serial.print("% | Pump: ");
  Serial.println(isPumpRunning ? "RUNNING" : "IDLE");

  // ==========================================
  //      PART 2: SENSORS & OLED (Code 2)
  // ==========================================
  readNPK();
  readOtherSensorsAndDisplay();
  
  // Combined Delay
  delay(2000); 
}

// ----------------- NPK FUNCTION -----------------
void readNPK() {
  // ---- TRANSMIT MODE ----
  digitalWrite(RE_PIN, HIGH);  
  digitalWrite(DE_PIN, HIGH);  
  delay(10);

  rs485.write(npkRequest, sizeof(npkRequest));
  rs485.flush();

  // ---- RECEIVE MODE ----
  digitalWrite(DE_PIN, LOW);   
  digitalWrite(RE_PIN, LOW);   
  delay(500); 

  int bytesAvailable = rs485.available();
  if(bytesAvailable >= 7) { 
    byte response[bytesAvailable];
    for(int i=0; i<bytesAvailable; i++) {
      response[i] = rs485.read();
    }

    // Extract NPK values
    N = response[3] / 10.0; 
    P = response[5] / 10.0;
    K = response[7] / 10.0;

    Serial.print("N: "); Serial.print(N); Serial.println(" mg/kg");
    Serial.print("P: "); Serial.print(P); Serial.println(" mg/kg");
    Serial.print("K: "); Serial.print(K); Serial.println(" mg/kg");
    Serial.println("----------------------");

  } else {
    Serial.println("No data from NPK sensor");
  }
}

// ----------------- Other Sensors & Display FUNCTION -----------------
void readOtherSensorsAndDisplay() {
  // DHT
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  // Soil moisture (For Display Logic)
  int rawMoist = analogRead(moisturePin);
  int moistPer = constrain(map(rawMoist, 3200, 1200, 0, 100), 0, 100);

  // pH
  int rawPH = analogRead(phPin);
  float voltage = rawPH * (2.5 / 4095.0); 
  float pHValue = 7.0 + (voltage - neutralVoltage) * slope;
  if(pHValue < 0) pHValue = 0;
  if(pHValue > 14) pHValue = 14;

  // SERIAL OUTPUT (Code 2 style)
  Serial.print("TEMP: "); Serial.print(t, 1); Serial.print(" C | ");
  Serial.print("HUMID: "); Serial.print(h, 1); Serial.print(" % | ");
  Serial.print("Mois: "); Serial.print(moistPer); Serial.print(" % | ");
  Serial.print("pH: "); Serial.println(pHValue,2);

  // --- OLED DISPLAY (compact, small font) ---
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);  // smallest font

  // Row 0: TEMP + HUMID
  display.setCursor(0, 0);
  display.print("Temp:"); display.print(t,1);
  display.print("C Humi:"); display.print((int)h); display.print("%");

  // Row 1: SOIL MOISTURE + STATUS
  display.setCursor(0, 12);
  display.print("Mois:"); display.print(moistPer); display.print("% ");
  if(moistPer < 30) display.print("THIRSTY");
  else if(moistPer > 65) display.print("WET");
  else display.print("HAPPY");

  // Row 2: pH
  display.setCursor(0, 24);
  display.print("PH:"); display.println(pHValue,1);

  // Row 3: N
  display.setCursor(0, 36);
  display.print("N:"); display.println(N);

  // Row 4: P
  display.setCursor(0, 44);
  display.print("P:"); display.println(P);

  // Row 5: K
  display.setCursor(0, 52);
  display.print("K:"); display.println(K);

  display.display();
}