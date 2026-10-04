#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// ==========================================
// PIN DEFINITIONS FOR ESP32-C3 SUPERMINI
// ==========================================
constexpr int I2C_SDA_PIN = 21;
constexpr int I2C_SCL_PIN = 22;
constexpr int ONBOARD_LED_PIN = 2; // SuperMini onboard blue LED (Active LOW)

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
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz Fast Mode for smooth 30 FPS animation

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
  static uint8_t testMode = 0;
  static int scanRowY = 0;
  static int pageIndex = 0;
  static unsigned long lastStepTime = 0;
  static unsigned long lastModeSwitch = 0;

  unsigned long now = millis();

  display.clearDisplay();

  // ==============================================================
  // MODE 0: SINGLE-ROW STEPPER (Y = 0 -> 63)
  // Sweeps one row at a time. Watch the screen! The moment a
  // horizontal line appears, check the number at the bottom.
  // ==============================================================
  if (testMode == 0) {
    // Step down by 1 row every 200 ms
    if (now - lastStepTime > 200) {
      lastStepTime = now;
      scanRowY++;
      if (scanRowY >= SCREEN_HEIGHT) {
        scanRowY = 0;
        testMode = 1; // Switch to Page Band Test
        pageIndex = 0;
        lastModeSwitch = now;
      }
    }

    // Draw the single horizontal test line across full width
    display.drawLine(0, scanRowY, SCREEN_WIDTH - 1, scanRowY, SSD1306_WHITE);

    // Telemetry Box ALWAYS in the safe bottom area (Y = 38 to 63)
    display.fillRect(0, 38, 128, 26, SSD1306_BLACK);
    display.drawRect(0, 38, 128, 26, SSD1306_WHITE);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(4, 42);
    display.printf("TESTING ROW Y = %2d", scanRowY);

    display.setCursor(4, 52);
    display.print("WATCH FOR FIRST LINE");
  }

  // ==============================================================
  // MODE 1: 8-PIXEL PAGE BAND HIGHLIGHTER
  // Highlights Page 0 (0-7), Page 1 (8-15), Page 2 (16-23)...
  // ==============================================================
  else if (testMode == 1) {
    if (now - lastStepTime > 800) {
      lastStepTime = now;
      pageIndex++;
      if (pageIndex >= 8) {
        testMode = 2; // Switch to Safe-Zone Preview
        lastModeSwitch = now;
      }
    }

    int bandStartY = pageIndex * 8;
    // Fill the 8-pixel tall band
    display.fillRect(0, bandStartY, SCREEN_WIDTH, 8, SSD1306_WHITE);

    // Telemetry Box in the bottom safe area
    display.fillRect(0, 38, 128, 26, SSD1306_BLACK);
    display.drawRect(0, 38, 128, 26, SSD1306_WHITE);

    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(4, 42);
    display.printf("BAND %d (Y: %d - %d)", pageIndex, bandStartY,
                   bandStartY + 7);

    display.setCursor(4, 52);
    if (pageIndex <= 1) {
      display.print("YELLOW ZONE (0-15)");
    } else {
      display.print("BLUE ZONE (16-63)");
    }
  }

  // ==============================================================
  // MODE 2: 9-BLOCK GRID WITH LABELS
  // Tests each 42x21 block of the 3x3 grid
  // ==============================================================
  else if (testMode == 2) {
    // Outer border
    display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

    // Vertical grid lines at X = 42 and X = 85
    display.drawLine(42, 0, 42, 63, SSD1306_WHITE);
    display.drawLine(85, 0, 85, 63, SSD1306_WHITE);

    // Horizontal grid lines at Y = 21 and Y = 42
    display.drawLine(0, 21, 127, 21, SSD1306_WHITE);
    display.drawLine(0, 42, 127, 42, SSD1306_WHITE);

    // Block Labels in Middle and Lower Rows
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Middle row (Y = 22 to 42)
    display.setCursor(14, 28);
    display.print("B4");
    display.setCursor(58, 28);
    display.print("B5");
    display.setCursor(100, 28);
    display.print("B6");

    // Lower row (Y = 43 to 63)
    display.setCursor(14, 50);
    display.print("B7");
    display.setCursor(58, 50);
    display.print("B8");
    display.setCursor(100, 50);
    display.print("B9");

    if (now - lastModeSwitch > 5000) {
      testMode = 0; // Loop back to Row Stepper
      scanRowY = 0;
      lastModeSwitch = now;
    }
  }

  display.display();
  delay(15);
}