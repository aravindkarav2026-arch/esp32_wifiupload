#define BLYNK_TEMPLATE_ID    "TMPL3pekN51Kj"
#define BLYNK_TEMPLATE_NAME  "Watertanklevel"
#define BLYNK_AUTH_TOKEN     "mGUWTvi_KRqoiftj8BK8YnMPn2A_9QoE"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <BlynkSimpleEsp32.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Preferences.h>
#include <time.h>
#include <algorithm>

// --- Wi-Fi Credentials ---
char ssid[] = "AKB -4G";
char pass[] = "ar20232023";

// --- NTP Time Settings (India Standard Time UTC+5:30) ---
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;    
const int daylightOffset_sec = 0;

// --- Pin Assignments (ESP32-C6) ---
#define TRIG_PIN     19
#define ECHO_PIN     18
#define RGB_LED_PIN  8    // Double-blinks twice per second
#define OLED_SDA     6    
#define OLED_SCL     7    
#define RELAY_PIN    22   // Relay module pin for 5V Buzzer Alarm

// --- Tank Dimensions (in cm) ---
const int TANK_FULL_DISTANCE  = 25;   
const int TANK_EMPTY_DISTANCE = 130;  

const char* mdns_hostname = "watertank";

// Hardware Objects
HardwareSerial SerialCOM8(1); 
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);
WebServer server(80);
BlynkTimer timer;
Preferences prefs;

// Global System Variables
uint32_t dailyBootCount = 0;
uint32_t dailyUptimeSeconds = 0;
uint32_t currentSessionStartSec = 0;
int currentYearDay = -1; 
int currentWaterPercent = -1;
bool alarmActive = false;

// Logging Helpers
template <typename T> void logPrint(T msg) { Serial.print(msg); SerialCOM8.print(msg); }
template <typename T> void logPrintln(T msg) { Serial.println(msg); SerialCOM8.println(msg); }
void logPrintf(const char* format, ...) {
  char loc_buf[128];
  va_list argptr;
  va_start(argptr, format);
  vsnprintf(loc_buf, sizeof(loc_buf), format, argptr);
  va_end(argptr);
  Serial.print(loc_buf);
  SerialCOM8.print(loc_buf);
}

// Non-blocking LED Double-Blink Logic (2 flashes within 1000ms cycle)
void handleLedBlink() {
  static unsigned long lastCycleStart = 0;
  unsigned long currentMillis = millis();
  unsigned long elapsed = (currentMillis - lastCycleStart) % 1000; // Repeat every 1 second

  // 1000ms pattern: Flash 1 (0-100ms) -> OFF (100-200ms) -> Flash 2 (200-300ms) -> OFF (300-1000ms)
  if ((elapsed >= 0 && elapsed < 100) || (elapsed >= 200 && elapsed < 300)) {
    #ifdef RGB_BUILTIN
      neopixelWrite(RGB_BUILTIN, 0, 0, 255); // Blue flash for Neopixel
    #else
      digitalWrite(RGB_LED_PIN, HIGH);
    #endif
  } else {
    #ifdef RGB_BUILTIN
      neopixelWrite(RGB_BUILTIN, 0, 0, 0);
    #else
      digitalWrite(RGB_LED_PIN, LOW);
    #endif
  }
}

// Check Date & Update Cumulative Daily Uptime in NVS
void updateDailyStats() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int today = timeinfo.tm_yday; 

    if (currentYearDay != today) {
      if (currentYearDay != -1) {
        dailyBootCount = 1;
        dailyUptimeSeconds = 0;
        
        prefs.putUInt("day", today);
        prefs.putUInt("boots", dailyBootCount);
        prefs.putUInt("uptime", dailyUptimeSeconds);
      }
      currentYearDay = today;
    }
  }

  uint32_t sessionSecs = (millis() - currentSessionStartSec) / 1000;
  uint32_t totalTodaySecs = dailyUptimeSeconds + sessionSecs;

  static uint32_t lastSave = 0;
  if (millis() - lastSave > 60000) {
    prefs.putUInt("uptime", totalTodaySecs);
    lastSave = millis();
  }
}

// Dynamic HTML Webpage for watertank.local
void handleRootWebPage() {
  uint32_t totalSecs = dailyUptimeSeconds + ((millis() - currentSessionStartSec) / 1000);
  uint32_t hrs = totalSecs / 3600;
  uint32_t mins = (totalSecs % 3600) / 60;

  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='5'>"; 
  html += "<title>ESP32-C6 Tank Dashboard</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background: #121212; color: #fff; text-align: center; padding: 20px; }";
  html += ".card { background: #1e1e1e; padding: 20px; border-radius: 12px; margin: 15px auto; max-width: 400px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }";
  html += "h1 { color: #00bcd4; margin-bottom: 5px; }";
  html += ".val { font-size: 48px; font-weight: bold; color: #4caf50; margin: 10px 0; }";
  html += ".err { font-size: 32px; color: #f44336; }";
  html += ".stat { font-size: 16px; color: #bbb; text-align: left; margin: 8px 0; }";
  html += ".alarm-on { color: #f44336; font-weight: bold; }";
  html += ".alarm-off { color: #4caf50; font-weight: bold; }";
  html += "input[type=file] { margin: 10px 0; }";
  html += "input[type=submit] { background: #00bcd4; border: none; color: white; padding: 10px 20px; border-radius: 5px; cursor: pointer; }";
  html += "</style></head><body>";

  html += "<div class='card'>";
  html += "<h1>Water Tank Level</h1>";
  if (currentWaterPercent >= 0) {
    html += "<div class='val'>" + String(currentWaterPercent) + "%</div>";
  } else {
    html += "<div class='val err'>SENSOR ERROR</div>";
  }
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>System Diagnostics</h3>";
  html += "<div class='stat'><b>Wi-Fi Status:</b> " + String((WiFi.status() == WL_CONNECTED) ? "Connected" : "Offline") + "</div>";
  html += "<div class='stat'><b>Buzzer Alarm (>=95%):</b> <span class='" + String(alarmActive ? "alarm-on'>ACTIVE (BUZZER ON)" : "alarm-off'>OFF") + "</span></div>";
  html += "<div class='stat'><b>Local IP:</b> " + WiFi.localIP().toString() + "</div>";
  html += "<div class='stat'><b>Boots Today:</b> " + String(dailyBootCount) + "</div>";
  html += "<div class='stat'><b>Uptime Today:</b> " + String(hrs) + "h " + String(mins) + "m (" + String(totalSecs / 60) + " mins)</div>";
  html += "</div>";

  html += "<div class='card'>";
  html += "<h3>Firmware Update (OTA)</h3>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data'>";
  html += "<input type='file' name='update' accept='.bin'><br>";
  html += "<input type='submit' value='Upload & Flash'>";
  html += "</form>";
  html += "</div>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

// OLED Display Renderer
void updateOLEDDisplay(int percent, const char* statusMsg) {
  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Status: ");
  display.println(statusMsg);

  display.setTextSize(2);
  display.setCursor(0, 12);
  if (percent >= 0) {
    display.print("Tank: ");
    display.print(percent);
    display.print("%");
  } else {
    display.print("SENS ERR");
  }

  display.drawFastHLine(0, 32, 128, SH110X_WHITE);

  uint32_t totalSecs = dailyUptimeSeconds + ((millis() - currentSessionStartSec) / 1000);
  uint32_t hrs = totalSecs / 3600;
  uint32_t mins = (totalSecs % 3600) / 60;

  display.setTextSize(1);
  display.setCursor(0, 36);
  if (alarmActive) {
    display.print("ALARM: HIGH WATER!");
  } else {
    display.printf("Boots Today : %u", dailyBootCount);
  }

  display.setCursor(0, 46);
  display.printf("Uptime Today: %uh %um", hrs, mins);

  display.setCursor(0, 56);
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    display.printf("IP: *.*.*.%d", ip[3]);
  } else {
    display.print("IP: Offline");
  }

  display.display();
}

int readSingleDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 15000);
  if (duration == 0) return -1;
  return duration * 0.034 / 2;
}

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
    delay(30);
  }

  if (validCount == 0) return -1;

  std::sort(rawReadings, rawReadings + validCount);
  int medianDistance = rawReadings[validCount / 2];
  medianDistance = constrain(medianDistance, TANK_FULL_DISTANCE, TANK_EMPTY_DISTANCE);

  int percent = map(medianDistance, TANK_EMPTY_DISTANCE, TANK_FULL_DISTANCE, 0, 100);
  return constrain(percent, 0, 100);
}

void checkAndSendTankLevel() {
  updateDailyStats(); 

  currentWaterPercent = getFilteredWaterLevelPercentage();
  const char* statusStr = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Offline";

  uint32_t totalUptimeSecs = dailyUptimeSeconds + ((millis() - currentSessionStartSec) / 1000);
  uint32_t dailyUptimeMinutes = totalUptimeSecs / 60;

  if (currentWaterPercent == -1) {
    logPrintln("Sensor Timeout / Reflection Error!");
    digitalWrite(RELAY_PIN, LOW); // Relay OFF during sensor errors
    alarmActive = false;
    updateOLEDDisplay(-1, statusStr);
  } else {
    logPrintf("Water Level: %d%%\n", currentWaterPercent);
    
    // Relay Alarm Logic for Tank >= 95%
    if (currentWaterPercent >= 95) {
      digitalWrite(RELAY_PIN, HIGH); // Turn Relay ON
      alarmActive = true;
      logPrintln("ALARM: Water level >= 95%! Buzzer activated.");
    } else {
      digitalWrite(RELAY_PIN, LOW);  // Turn Relay OFF
      alarmActive = false;
    }

    updateOLEDDisplay(currentWaterPercent, statusStr);
  }

  // Send all metrics to Blynk Cloud
  if (Blynk.connected()) {
    if (currentWaterPercent >= 0) {
      Blynk.virtualWrite(V0, currentWaterPercent);  // Water Level (%)
    }
    Blynk.virtualWrite(V1, dailyBootCount);         // Boots Count Today
    Blynk.virtualWrite(V2, dailyUptimeMinutes);     // Uptime Today (Minutes)
    Blynk.virtualWrite(V3, alarmActive ? 1 : 0);    // Relay Alarm Status (V3)
  }
}

void setup() {
  Serial.begin(115200);
  SerialCOM8.begin(115200, SERIAL_8N1, 17, 16); 
  delay(1000);

  currentSessionStartSec = millis();

  // Initialize Pins
  pinMode(RGB_LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Start with Relay OFF

  // Load Stored NVS Data
  prefs.begin("stats", false);
  currentYearDay = prefs.getUInt("day", -1);
  dailyBootCount = prefs.getUInt("boots", 0) + 1;
  dailyUptimeSeconds = prefs.getUInt("uptime", 0);

  prefs.putUInt("boots", dailyBootCount);

  // Initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(0x3C, true);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Aravind, ESP32-C6 Booting...");
  display.printf("Boot #%u Today\n", dailyBootCount);
  display.display();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Connect Wi-Fi & Time
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart < 15000)) {
    handleLedBlink(); // Keep double-blinking during Wi-Fi connect
    delay(10);
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    if (MDNS.begin(mdns_hostname)) {
      MDNS.addService("http", "tcp", 80);
    }
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(3000); 
  }

  // Web Server Routes
  server.on("/", HTTP_GET, handleRootWebPage);

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "OTA FAIL" : "OTA SUCCESS");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      prefs.putUInt("uptime", dailyUptimeSeconds + ((millis() - currentSessionStartSec) / 1000));
      Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      Update.end(true);
    }
  });

  server.begin();
  timer.setInterval(1000L, checkAndSendTankLevel);
}

void loop() {
  handleLedBlink(); // Continuously processes non-blocking 2-flash/sec pattern
  server.handleClient();
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }
  timer.run();
}
