#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#define SDA_PIN 6
#define SCL_PIN 7

// Address 0x3C is standard for 0.96 SH1106 OLEDs
Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize I2C on ESP32-C6 pins
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize SH1106 display
  display.begin(0x3C, true); // 0x3C I2C address, reset=true
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 10);
  display.println("SH1106 DISPLAY OK!");
  
  display.setTextSize(2);
  display.setCursor(0, 30);
  display.println("WORKING!");

  display.display(); // Push buffer to screen
}

void loop() {
}
