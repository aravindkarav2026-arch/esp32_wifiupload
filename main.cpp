#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>

// --- Wi-Fi Credentials ---
const char* ssid     = "AKB -4G";
const char* password = "ar20232023";

// Target Firmware Version & URL for Auto-Pull updates
const char* CURRENT_VERSION = "1.0.0";
const char* FIRMWARE_URL    = "http://YOUR_SERVER_OR_GITHUB_RELEASE_URL/firmware.bin";

WebServer server(80);

// Simple HTML page for manual web uploads
const char* uploadPage = 
  "<form method='POST' action='/update' enctype='multipart/form-data'>"
    "<h2>ESP32-C6 N16 OTA Firmware Updater</h2>"
    "<p>Select compiled firmware.bin file:</p>"
    "<input type='file' name='update' accept='.bin'>"
    "<input type='submit' value='Flash Firmware Wireless'>"
  "</form>";

// Function to trigger direct HTTP download & flash from URL
void checkForHttpUpdate(const char* binUrl) {
  Serial.println("Starting HTTP OTA Update from URL...");
  HTTPClient http;
  
  // Follow redirects (needed for GitHub release downloads)
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(binUrl);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    bool canBegin = Update.begin(contentLength);

    if (canBegin) {
      Serial.println("Downloading and Flashing...");
      WiFiClient* client = http.getStreamPtr();
      size_t written = Update.writeStream(*client);

      if (written == contentLength) {
        Serial.println("Written successfully!");
      } else {
        Serial.printf("Written only %d/%d bytes\n", written, contentLength);
      }

      if (Update.end()) {
        if (Update.isFinished()) {
          Serial.println("OTA Update Complete! Rebooting...");
          ESP.restart();
        }
      } else {
        Serial.printf("Error Occurred: %d\n", Update.getError());
      }
    } else {
      Serial.println("Not enough space to start OTA update.");
    }
  } else {
    Serial.printf("HTTP GET failed, error code: %d\n", httpCode);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);

  // USB-CDC startup delay
  unsigned long start = millis();
  while (!Serial && (millis() - start < 3000));

  Serial.printf("\n--- ESP32-C6 N16 (Flash: 16MB) - Firmware v%s ---\n", CURRENT_VERSION);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("Local IP Address: http://");
  Serial.println(WiFi.localIP());

// Route 1: Serve Web Interface
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", uploadPage);
  });

  // Route 2: Trigger Manual HTTP Download Check
  server.on("/check-update", HTTP_GET, []() {
    server.send(200, "text/plain", "Checking for updates...");
    checkForHttpUpdate(FIRMWARE_URL);
  });

  // Route 3: Handle Drag-and-Drop Web Upload (Updated for ESP32 Arduino Core v3.x)
  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", (Update.hasError()) ? "OTA FAIL" : "OTA SUCCESS - Rebooting...");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload(); // Fixed: server.upload() instead of server.arg(0)
    
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update start: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      // Fixed: upload.currentSize instead of upload.currentLength
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("Update Successful: %u bytes\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
}

void loop() {
  server.handleClient();
  delay(2);
}
