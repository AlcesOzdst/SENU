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
  static float angleDeg = 0.0f;
  static uint32_t frameCount = 0;
  static unsigned long lastSerialLog = 0;
  static bool ledState = false;

  display.clearDisplay();

  // -------------------------------------------------------------
  // 1. OUTER BORDER (Tests display matrix bounds: 0,0 to 127,63)
  // -------------------------------------------------------------
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  // -------------------------------------------------------------
  // 2. STATIC CROSS & RETICLE (Centered at CX=64, CY=32)
  // -------------------------------------------------------------
  // Horizontal arm with tick marks
  display.drawLine(CX - 26, CY, CX + 26, CY, SSD1306_WHITE);
  display.drawLine(CX - 26, CY - 2, CX - 26, CY + 2, SSD1306_WHITE);
  display.drawLine(CX + 26, CY - 2, CX + 26, CY + 2, SSD1306_WHITE);

  // Vertical arm with tick marks
  display.drawLine(CX, CY - 24, CX, CY + 24, SSD1306_WHITE);
  display.drawLine(CX - 2, CY - 24, CX + 2, CY - 24, SSD1306_WHITE);
  display.drawLine(CX - 2, CY + 24, CX + 2, CY + 24, SSD1306_WHITE);

  // Static central circular target reticle
  display.drawCircle(CX, CY, 10, SSD1306_WHITE);
  display.drawPixel(CX, CY, SSD1306_WHITE);

  // -------------------------------------------------------------
  // 3. ROTATING ELEMENT (Smooth continuous rotation around cross)
  // -------------------------------------------------------------
  float rad = angleDeg * (PI / 180.0f);

  // Rotating needle vector
  int xTip = CX + (int)(ROT_RADIUS * cos(rad));
  int yTip = CY + (int)(ROT_RADIUS * sin(rad));
  display.drawLine(CX, CY, xTip, yTip, SSD1306_WHITE);

  // Orbiting satellite diamond / marker at tip
  int xOrbit = CX + (int)(ORBIT_RADIUS * cos(rad));
  int yOrbit = CY + (int)(ORBIT_RADIUS * sin(rad));
  display.fillRect(xOrbit - 2, yOrbit - 2, 5, 5, SSD1306_WHITE);

  // Counter-rotating orbital dot on the opposite side (180 deg out of phase)
  float radOpposite = rad + PI;
  int xOpposite = CX + (int)((ORBIT_RADIUS - 6) * cos(radOpposite));
  int yOpposite = CY + (int)((ORBIT_RADIUS - 6) * sin(radOpposite));
  display.drawCircle(xOpposite, yOpposite, 2, SSD1306_WHITE);

  // -------------------------------------------------------------
  // 4. CORNER TELEMETRY LABELS
  // -------------------------------------------------------------
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Top-left label
  display.setCursor(4, 4);
  display.print("SENU");

  // Top-right label
  display.setCursor(94, 4);
  display.print("0.96\"");

  // Bottom-left angle indicator
  display.setCursor(4, 52);
  display.printf("%3d", (int)angleDeg);
  display.drawCircle(25, 53, 1, SSD1306_WHITE); // Degree symbol

  // Bottom-right frame indicator
  display.setCursor(76, 52);
  display.printf("#%lu", frameCount % 10000);

  // Commit frame buffer to OLED over I2C
  display.display();

  // -------------------------------------------------------------
  // 5. MOTION UPDATE & LOGGING
  // -------------------------------------------------------------
  angleDeg += 4.0f; // 4 degrees per frame (~90 frames for full 360 deg turn)
  if (angleDeg >= 360.0f) {
    angleDeg -= 360.0f;
  }
  frameCount++;

  // Log heartbeat to Serial and toggle LED every second
  unsigned long now = millis();
  if (now - lastSerialLog >= 1000) {
    lastSerialLog = now;
    ledState = !ledState;
    digitalWrite(ONBOARD_LED_PIN,
                 ledState ? LOW : HIGH); // Active-low heartbeat

    Serial.printf("[OLED Alive] Uptime: %lus | Frames: %lu | Angle: %d deg | "
                  "Addr: 0x%02X\n",
                  now / 1000, frameCount, (int)angleDeg, oledAddress);
  }

  delay(30); // ~30-33 FPS refresh rate
}
