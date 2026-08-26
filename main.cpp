#define BLYNK_TEMPLATE_ID    "TMPL3pekN51Kj"
#define BLYNK_TEMPLATE_NAME  "Watertanklevel"
#define BLYNK_AUTH_TOKEN     "mGUWTvi_KRqoiftj8BK8YnMPn2A_9QoE"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <BlynkSimpleEsp32.h>
#include <algorithm> // For median filtering

// --- Wi-Fi Credentials ---
char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// --- Pin Assignments (ESP32-C6) ---
#define TRIG_PIN     19
#define ECHO_PIN     18
#define RGB_LED_PIN  8   // Onboard WS2812 RGB LED

// --- Tank Dimensions (in cm) ---
const int TANK_FULL_DISTANCE  = 20;   // 100% full (sensor to water level)
const int TANK_EMPTY_DISTANCE = 150;  // 0% full (sensor to tank bottom)

WebServer server(80);
BlynkTimer timer;

// Drag-and-Drop Web Upload Page HTML
const char* uploadPage = 
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<h2>ESP32-C6 Water Tank & OTA Firmware Updater</h2>"
    "<p>Select new compiled firmware.bin file:</p>"
    "<input type='file' name='update' accept='.bin'>"
    "<input type='submit' value='Upload & Flash Wirelessly'>"
  "</form>";

// Onboard RGB LED Control
void setBoardRGB(uint8_t r, uint8_t g, uint8_t b) {
  #ifdef RGB_BUILTIN
    neopixelWrite(RGB_BUILTIN, r, g, b);
  #else
    neopixelWrite(RGB_LED_PIN, r, g, b);
  #endif
}

// Single Ultrasonic Distance Pulse
int readSingleDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 15000); // 15ms timeout (~2.5m range)
  if (duration == 0) return -1;

  return duration * 0.034 / 2;
}

// Median Filtered Water Level Calculation
int getFilteredWaterLevelPercentage() {
  const int SAMPLES = 5;
  int rawReadings[SAMPLES];
  int validCount = 0;

  for (int i = 0; i < SAMPLES; i++) {
    int dist = readSingleDistanceCm();
    if (dist > 0) {
      rawReadings[validCount] = dist;
      validCount++;
    }
    delay(10); // Gap between acoustic pulses to prevent internal tank echoes
  }

  if (validCount == 0) return -1;

  // Sort array to get median
  std::sort(rawReadings, rawReadings + validCount);
  int medianDistance = rawReadings[validCount / 2];

  // Constrain distance to defined tank limits FIRST
  medianDistance = constrain(medianDistance, TANK_FULL_DISTANCE, TANK_EMPTY_DISTANCE);

  // Map distance to percentage (150cm = 0%, 20cm = 100%)
  return map(medianDistance, TANK_EMPTY_DISTANCE, TANK_FULL_DISTANCE, 0, 100);
}

// Timer Task: Runs every 1 second
void checkAndSendTankLevel() {
  int percent = getFilteredWaterLevelPercentage();

  if (percent == -1) {
    Serial.println("Sensor Timeout / Reflection Error!");
    return;
  }

  Serial.printf("Water Level: %d%%\n", percent);
  
  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, percent);
  }

  // Update Onboard RGB LED based on water level status
  if (percent <= 30) {
    setBoardRGB(255, 0, 0);   // RED (0% - 30%: Low Level)
  } else if (percent <= 70) {
    setBoardRGB(255, 255, 0); // YELLOW (31% - 70%: Medium Level)
  } else {
    setBoardRGB(0, 255, 0);   // GREEN (71% - 100%: Full Level)
  }
}

void setup() {
  Serial.begin(115200);

  // Native USB-CDC (COM7) setup delay
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000));

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Non-blocking Wi-Fi and Blynk connection
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect(5000);

  Serial.println("\nWiFi Connecting...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("Web OTA URL: http://");
  Serial.println(WiFi.localIP());

  // Web OTA Server Routes (Compatible with ESP32 Core v3.x)
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", uploadPage);
  });

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "OTA FAIL" : "OTA SUCCESS - Rebooting...");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("OTA Start: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("OTA Success: %u bytes\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.begin();

  // Run tank level check every 1000ms (1 second)
  timer.setInterval(1000L, checkAndSendTankLevel);
}

void loop() {
  server.handleClient();
  if (Blynk.connected()) {
    Blynk.run();
  }
  timer.run();
}
