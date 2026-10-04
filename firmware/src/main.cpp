#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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
bool oledFound = false;
bool rtcFound = false;
uint8_t foundDevices[8];
uint8_t foundDeviceCount = 0;

void scanI2CBus() {
  Serial.println("\n--- Starting I2C Bus Scan (SDA: GPIO 4, SCL: GPIO 5) ---");
  foundDeviceCount = 0;
  oledFound = false;
  rtcFound = false;

  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.printf("  [+] I2C device found at address 0x%02X", address);
      if (address == 0x3C || address == 0x3D) {
        Serial.print(" (SSD1306 OLED Display)");
        oledAddress = address;
        oledFound = true;
      } else if (address == 0x68) {
        Serial.print(" (DS3231 RTC Module)");
        rtcFound = true;
      }
      Serial.println();

      if (foundDeviceCount < sizeof(foundDevices)) {
        foundDevices[foundDeviceCount++] = address;
      }
    }
  }

  if (foundDeviceCount == 0) {
    Serial.println("  [!] WARNING: No I2C devices found! Check wiring, pull-ups, and power.");
  } else {
    Serial.printf("--- Scan Complete. Total devices found: %u ---\n\n", foundDeviceCount);
  }
}

void setup() {
  pinMode(ONBOARD_LED_PIN, OUTPUT);
  digitalWrite(ONBOARD_LED_PIN, HIGH); // LED OFF initially (active low)

  Serial.begin(115200);
  delay(1500); // Allow USB CDC serial monitor to attach

  Serial.println("==========================================");
  Serial.println("   SENU — ESP32-C3 Hardware Diagnostics  ");
  Serial.println("   Milestone M0: OLED & I2C Verification  ");
  Serial.println("==========================================");

  // Initialize I2C with explicit pins for SuperMini
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000); // 400kHz Fast Mode

  scanI2CBus();

  // Try discovered OLED address first, fallback to 0x3C then 0x3D
  bool displayInitialized = false;
  if (oledFound) {
    displayInitialized = display.begin(SSD1306_SWITCHCAPVCC, oledAddress);
  }
  if (!displayInitialized) {
    displayInitialized = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    if (displayInitialized) oledAddress = 0x3C;
  }
  if (!displayInitialized) {
    displayInitialized = display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
    if (displayInitialized) oledAddress = 0x3D;
  }

  if (!displayInitialized) {
    Serial.println("\n[FATAL] SSD1306 allocation failed. Check VCC (3.3V/5V), GND, SDA(4), SCL(5).");
    while (true) {
      // Rapid SOS blink on failure
      digitalWrite(ONBOARD_LED_PIN, LOW);  delay(100);
      digitalWrite(ONBOARD_LED_PIN, HIGH); delay(100);
    }
  }

  Serial.printf("[SUCCESS] SSD1306 OLED initialized at address 0x%02X!\n", oledAddress);

  // Render Splash & Diagnostic Test
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Draw Header
  display.setTextSize(1);
  display.setCursor(16, 2);
  display.println("SENU APPLIANCE");
  display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

  // Status Info
  display.setCursor(0, 18);
  display.printf("OLED: OK (0x%02X)\n", oledAddress);
  display.printf("RTC : %s\n", rtcFound ? "FOUND (0x68)" : "NOT CONNECTED");
  display.printf("I2C Devices: %u\n", foundDeviceCount);

  // Draw full frame boundary to verify screen resolution and edges
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.setCursor(4, 52);
  display.print("Milestone M0 Ready");

  display.display();
  delay(3000);
}

void loop() {
  static unsigned long lastSecond = 0;
  static uint32_t counter = 0;
  static bool ledState = false;

  unsigned long currentMillis = millis();

  if (currentMillis - lastSecond >= 1000) {
    lastSecond = currentMillis;
    counter++;

    // Toggle onboard LED heartbeat (active low)
    ledState = !ledState;
    digitalWrite(ONBOARD_LED_PIN, ledState ? LOW : HIGH);

    // Refresh dynamic test display
    display.clearDisplay();

    // Outer border to verify (0,0) to (127,63) pixel bounds
    display.drawRect(0, 0, 128, 64, SSD1306_WHITE);

    // Title Bar
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(4, 4);
    display.print("SENU M0: OLED TEST");
    display.drawLine(2, 14, 125, 14, SSD1306_WHITE);

    // Diagnostics
    display.setCursor(4, 18);
    display.printf("Addr: 0x%02X | 400kHz\n", oledAddress);

    display.setCursor(4, 28);
    display.printf("Uptime: %lus", currentMillis / 1000);

    display.setCursor(4, 38);
    display.printf("Frames: #%lu", counter);

    // Heartbeat indicator and animated progress bar
    int barWidth = (counter % 20) * 6;
    display.drawRect(4, 50, 120, 8, SSD1306_WHITE);
    display.fillRect(6, 52, barWidth, 4, SSD1306_WHITE);

    display.display();

    // Serial Heartbeat
    Serial.printf("[Heartbeat #%lu] Uptime: %lus | OLED: 0x%02X | LED: %s\n",
                  counter, currentMillis / 1000, oledAddress, ledState ? "ON" : "OFF");
  }

  delay(20);
}
