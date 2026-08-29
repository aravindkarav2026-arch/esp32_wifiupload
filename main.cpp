#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Hardware I2C Pins for ESP32-C6
#define SDA_PIN 6
#define SCL_PIN 7

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C // Default I2C address (can be 0x3D on some modules)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize I2C bus with specific ESP32-C6 pins
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed! Check wiring or I2C address."));
    for (;;); // Don't proceed, loop forever
  }

  Serial.println(F("OLED Initialized Successfully!"));

  // Clear buffer
  display.clearDisplay();

  // Draw Test Text
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("ESP32-C6 OLED TEST"));
  
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(F("128x64 OK"));

  display.setTextSize(1);
  display.setCursor(0, 50);
  display.println(F("Status: Active"));

  // Render to physical screen
  display.display();
}

void loop() {
  // Nothing needed in loop for static test
}
