#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// ==========================================
// PIN DEFINITIONS FOR ESP32-C3 SUPERMINI
// ==========================================
constexpr int I2C_SDA_PIN = 4;
constexpr int I2C_SCL_PIN = 5;
constexpr int ONBOARD_LED_PIN = 8; // SuperMini onboard blue LED (Active LOW)

constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int OLED_RESET = -1;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

uint8_t oledAddress = 0x3C;

// Center coordinates for 128x64 display
constexpr int CX = 64;
constexpr int CY = 32;

// Arm length for rotating indicator
constexpr float ROT_RADIUS = 24.0f;
constexpr float ORBIT_RADIUS = 20.0f;
constexpr int SCREEN_Y_OFFSET = 22; // Skip unlit blocks R1-R3 (Y = 0..21)
constexpr int USABLE_HEIGHT = 42;   // Working height: 64 - 22 = 42 pixels

void setup() {
  pinMode(ONBOARD_LED_PIN, OUTPUT);
  digitalWrite(ONBOARD_LED_PIN, HIGH); // LED OFF initially

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n==========================================");
  Serial.println("   SENU: 0.96\" OLED Visual Motion Test    ");
  Serial.println("   Static Cross + Rotating Element Test   ");
  Serial.println("==========================================");

  // Initialize I2C bus explicitly
  // Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  // Wire.setClock(400000); // 400kHz Fast Mode for smooth 30 FPS animation
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000); // 100kHz standard mode

  // Try standard SSD1306 address 0x3C, fallback to 0x3D
  bool displayReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (displayReady) {
    oledAddress = 0x3C;
  } else {
    displayReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
    if (displayReady) {
      oledAddress = 0x3D;
    }
  }

  if (!displayReady) {
    Serial.println("[FATAL] SSD1306 OLED not found at 0x3C or 0x3D!");
    Serial.println("Check: SDA->GPIO 4, SCL->GPIO 5, VCC->3.3V, GND->GND");
    while (true) {
      // Rapid SOS blink on failure
      digitalWrite(ONBOARD_LED_PIN, LOW);
      delay(100);
      digitalWrite(ONBOARD_LED_PIN, HIGH);
      delay(100);
    }
  }

  Serial.printf(
      "[SUCCESS] SSD1306 OLED online at 0x%02X! Starting animation...\n",
      oledAddress);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(18, 20);
  display.println("OLED DETECTED!");
  display.setCursor(18, 36);
  display.printf("Address: 0x%02X", oledAddress);
  display.display();
  delay(1500);
}
void loop() {
  static unsigned long lastUpdate = 0;
  static uint32_t counter = 0;
  static bool ledState = false;

  unsigned long now = millis();

  if (now - lastUpdate >= 1000) {
    lastUpdate = now;
    counter++;

    // Toggle onboard LED heartbeat (Active LOW on SuperMini)
    ledState = !ledState;
    digitalWrite(ONBOARD_LED_PIN, ledState ? LOW : HIGH);

    display.clearDisplay();

    // 1. Boundary box for the active working area (Y = 22 to 63)
    display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT,
                     SSD1306_WHITE);

    // 2. Telemetry and text strictly in the working window
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(4, 25);
    display.print("C3 SUPERMINI : ACTIVE");

    display.setCursor(4, 35);
    display.printf("OLED 128x42  | Addr 0x%02X", oledAddress);

    display.setCursor(4, 45);
    display.printf("Uptime: %lus  #%lu", now / 1000, counter);

    // Dynamic animated progress bar at bottom of the active area
    int barWidth = (counter % 20) * 6;
    display.drawRect(4, 55, 120, 6, SSD1306_WHITE);
    display.fillRect(6, 57, barWidth, 2, SSD1306_WHITE);

    display.display();

    Serial.printf("[C3 Alive] Uptime: %lus | Frames: %lu | OLED: 0x%02X\n",
                  now / 1000, counter, oledAddress);
  }

  delay(20);
}
