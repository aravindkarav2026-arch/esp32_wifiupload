#define BLYNK_TEMPLATE_ID    "TMPL3pekN51Kj"
#define BLYNK_TEMPLATE_NAME  "Watertanklevel"
#define BLYNK_AUTH_TOKEN     "mGUWTvi_KRqoiftj8BK8YnMPn2A_9QoE"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <BlynkSimpleEsp32.h>
#include <ESPmDNS.h>
#include <algorithm> // For median filtering

// --- Wi-Fi Credentials ---
char ssid[] = "AKB -4G";
char pass[] = "ar20232023";

// --- Pin Assignments (ESP32-C6) ---
#define TRIG_PIN     19
#define ECHO_PIN     18
#define RGB_LED_PIN  8   // Onboard WS2812 RGB LED

// --- Tank Dimensions (in cm) ---
const int TANK_FULL_DISTANCE  = 25;   // 100% full (sensor to water level)
const int TANK_EMPTY_DISTANCE = 130;  // 0% full (sensor to tank bottom)

// --- Configuration ---
const char* mdns_hostname = "watertank"; // Becomes http://watertank.local

// Initialize secondary hardware serial on UART1 (avoiding UART0 collision with Serial)
HardwareSerial SerialCOM8(1); 

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

// Generic Template Log Helpers (Handles any data type on both COM ports)
template <typename T>
void logPrint(T message) {
  Serial.print(message);
  SerialCOM8.print(message);
}

template <typename T>
void logPrintln(T message) {
  Serial.println(message);
  SerialCOM8.println(message);
}

void logPrintf(const char* format, ...) {
  char loc_buf[128];
  va_list argptr;
  va_start(argptr, format);
  vsnprintf(loc_buf, sizeof(loc_buf), format, argptr);
  va_end(argptr);
  
  Serial.print(loc_buf);
  SerialCOM8.print(loc_buf);
}


// Onboard RGB LED Control (WS2812 color sequence fix: G, R, B)
void setBoardRGB(uint8_t r, uint8_t g, uint8_t b) {
  #ifdef RGB_BUILTIN
    neopixelWrite(RGB_BUILTIN, g, r, b); // Swapped r and g
  #else
    neopixelWrite(RGB_LED_PIN, g, r, b);  // Swapped r and g
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
    delay(30); // Gap between acoustic pulses to prevent internal tank echoes
  }

  if (validCount == 0) return -1;

  // Sort array to extract median
  std::sort(rawReadings, rawReadings + validCount);
  int medianDistance = rawReadings[validCount / 2];

  // Constrain distance to defined tank limits FIRST (20cm to 150cm)
  medianDistance = constrain(medianDistance, TANK_FULL_DISTANCE, TANK_EMPTY_DISTANCE);

  // Map distance to percentage (150cm = 0%, 20cm = 100%) and constrain output range
  int percent = map(medianDistance, TANK_EMPTY_DISTANCE, TANK_FULL_DISTANCE, 0, 100);
  return constrain(percent, 0, 100);
}

// Timer Task: Runs every 1 second
void checkAndSendTankLevel() {
  int percent = getFilteredWaterLevelPercentage();

  if (percent == -1) {
    logPrintln("Sensor Timeout / Reflection Error!");
    return;
  }

  logPrintf("Water Level: %d%%\n", percent);
  
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
  // 1. Initialize Native USB Serial
  Serial.begin(115200);

  // 2. Initialize CH343 Hardware UART on UART1 (GPIO 16/17)
  SerialCOM8.begin(115200, SERIAL_8N1, 17, 16); 

  delay(1000);

  // Native USB-CDC setup delay (timeout 3s)
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000));

  logPrintln("\n--- Logging active on COM ports ---");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // 3. Set Wi-Fi Mode and start non-blocking connection attempt
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  logPrintln("WiFi Connecting...");
  
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart < 15000)) {
    delay(500);
    logPrint("."); 
  }

  if (WiFi.status() == WL_CONNECTED) {
    logPrintln("\nWiFi Connected!");
    logPrint("Local IP Address: http://");
    logPrintln(WiFi.localIP());

    // 2. Start mDNS Responder
    if (MDNS.begin(mdns_hostname)) {
      logPrintf("mDNS Responder Started! Access at: http://%s.local\n", mdns_hostname);
      
      // 3. Register HTTP Service on Port 80
      MDNS.addService("http", "tcp", 80);
    } else {
      logPrintln("Error setting up mDNS responder!");
    }

    // 4. Configure and connect to Blynk
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(3000); 
  } else {
    logPrintln("\nWiFi Connection Failed! Proceeding in offline mode...");
  }

  // 5. Start Web Server routes
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", uploadPage);
  });

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "OTA FAIL" : "OTA SUCCESS - Rebooting...");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    
    if (upload.status == UPLOAD_FILE_START) {
      logPrintf("OTA Start: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        Update.printError(SerialCOM8);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
        Update.printError(SerialCOM8);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        logPrintf("OTA Success: %u bytes\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
        Update.printError(SerialCOM8);
      }
    }
  });

  server.begin();

  // 6. Start timer task (1000ms interval)
  timer.setInterval(1000L, checkAndSendTankLevel);
}

void loop() {
  server.handleClient();
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }
  timer.run();
}
