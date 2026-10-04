#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

// ==========================================
// PIN DEFINITIONS FOR ESP32-C3 SUPERMINI
// ==========================================
constexpr int I2C_SDA_PIN = 4;
constexpr int I2C_SCL_PIN = 5;
constexpr int ONBOARD_LED_PIN = 8; // SuperMini blue LED (Active LOW)

// Rotary Encoder Pins
constexpr int ENCODER_CLK_PIN = 0; // Quadrature A (Interrupt)
constexpr int ENCODER_DT_PIN = 1;  // Quadrature B
constexpr int ENCODER_SW_PIN = 3;  // Push switch (Active LOW)

// Display Calibrated Geometry
constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int SCREEN_Y_OFFSET = 22; // Skip unlit top blocks (Y = 0..21)
constexpr int USABLE_HEIGHT = 42;   // Active height: 64 - 22 = 42 pixels

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
uint8_t oledAddress = 0x3C;

// ==========================================
// ENCODER & NAVIGATION STATE
// ==========================================
volatile int encoderDelta = 0;
int lastClkState = HIGH;

void IRAM_ATTR handleEncoderISR() {
  static unsigned long lastInterruptTime = 0;
  unsigned long now = millis();
  if (now - lastInterruptTime > 4) { // 4ms debounce
    int currentClk = digitalRead(ENCODER_CLK_PIN);
    int currentDt = digitalRead(ENCODER_DT_PIN);
    if (currentClk != lastClkState && currentClk == LOW) {
      if (currentDt != currentClk) {
        encoderDelta++;
      } else {
        encoderDelta--;
      }
    }
    lastClkState = currentClk;
    lastInterruptTime = now;
  }
}

// App Screens
enum ScreenState {
  SCREEN_MENU,
  SCREEN_FOCUS_TIMER,
  SCREEN_PC_METRICS,
  SCREEN_TASKS
};

ScreenState currentScreen = SCREEN_MENU;

// Menu items
const char *menuItems[] = {"1. Focus Timer", "2. PC Metrics", "3. Tasks (Sync)",
                           "4. Device Info"};
constexpr int TOTAL_MENU_ITEMS = sizeof(menuItems) / sizeof(menuItems[0]);
int selectedMenuIndex = 0;

void setup() {
  pinMode(ONBOARD_LED_PIN, OUTPUT);
  digitalWrite(ONBOARD_LED_PIN, HIGH);

  // Setup Rotary Encoder Pins
  pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);
  lastClkState = digitalRead(ENCODER_CLK_PIN);
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK_PIN), handleEncoderISR,
                  CHANGE);

  Serial.begin(115200);
  delay(1000);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  bool displayReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!displayReady) {
    display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }

  display.clearDisplay();
  display.display();
}

void renderMenu() {
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT,
                   SSD1306_WHITE);

  // Header Title at Y = 24
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 24);
  display.print("SENU: MAIN MENU");
  display.drawLine(2, 33, 125, 33, SSD1306_WHITE);

  // Calculate visible slice (show 2 items at a time inside the 42px window)
  int startIndex = (selectedMenuIndex >= 2) ? selectedMenuIndex - 1 : 0;
  if (startIndex > TOTAL_MENU_ITEMS - 2)
    startIndex = TOTAL_MENU_ITEMS - 2;

  for (int i = 0; i < 2; ++i) {
    int itemIdx = startIndex + i;
    int yPos = 36 + (i * 12);

    if (itemIdx == selectedMenuIndex) {
      // Inverted highlight bar for selected item
      display.fillRect(2, yPos - 1, 124, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.setCursor(6, yPos + 1);
      display.printf("> %s", menuItems[itemIdx]);
    } else {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(6, yPos + 1);
      display.printf("  %s", menuItems[itemIdx]);
    }
  }

  // Scroll indicator bar on the right edge
  int scrollY = 36 + (selectedMenuIndex * 6);
  display.fillRect(123, scrollY, 2, 6, SSD1306_WHITE);
}

void renderFocusTimer() {
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT,
                   SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 24);
  display.print("FOCUS SESSION (POMO)");
  display.drawLine(2, 33, 125, 33, SSD1306_WHITE);

  // Large timer readout
  display.setTextSize(2);
  display.setCursor(34, 37);
  display.print("25:00");

  display.setTextSize(1);
  display.setCursor(14, 54);
  display.print("[Click to Return]");
}

void renderPCMetrics() {
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT,
                   SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 24);
  display.print("HOST PC METRICS");
  display.drawLine(2, 33, 125, 33, SSD1306_WHITE);

  display.setCursor(6, 36);
  display.print("CPU:  18% [|||       ]");

  display.setCursor(6, 46);
  display.print("RAM:  42% [|||||     ]");

  display.setCursor(6, 55);
  display.print("[Click to Return]");
}

void renderTasks() {
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT,
                   SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 24);
  display.print("UPCOMING TASKS");
  display.drawLine(2, 33, 125, 33, SSD1306_WHITE);

  display.setCursor(6, 37);
  display.print("* Submit Report (2h)");

  display.setCursor(6, 48);
  display.print("* PCB Review (Tomorrow)");
}

void loop() {
  static unsigned long lastButtonCheck = 0;
  static int lastButtonState = HIGH;
  static bool ledState = false;

  unsigned long now = millis();

  // 1. Process Rotary Encoder Rotation
  if (encoderDelta != 0) {
    noInterrupts();
    int delta = encoderDelta;
    encoderDelta = 0;
    interrupts();

    if (currentScreen == SCREEN_MENU) {
      selectedMenuIndex = (selectedMenuIndex + delta) % TOTAL_MENU_ITEMS;
      if (selectedMenuIndex < 0)
        selectedMenuIndex += TOTAL_MENU_ITEMS;
    }
  }

  // 2. Process Encoder Pushbutton (SW)
  int currentButtonState = digitalRead(ENCODER_SW_PIN);
  if (currentButtonState == LOW && lastButtonState == HIGH &&
      (now - lastButtonCheck > 250)) {
    lastButtonCheck = now;

    // Toggle onboard LED on click
    ledState = !ledState;
    digitalWrite(ONBOARD_LED_PIN, ledState ? LOW : HIGH);

    if (currentScreen == SCREEN_MENU) {
      if (selectedMenuIndex == 0)
        currentScreen = SCREEN_FOCUS_TIMER;
      else if (selectedMenuIndex == 1)
        currentScreen = SCREEN_PC_METRICS;
      else if (selectedMenuIndex == 2)
        currentScreen = SCREEN_TASKS;
      else
        currentScreen = SCREEN_MENU;
    } else {
      currentScreen = SCREEN_MENU; // Any sub-screen click returns to menu
    }
  }
  lastButtonState = currentButtonState;

  // 3. Render Active Screen
  display.clearDisplay();

  switch (currentScreen) {
  case SCREEN_MENU:
    renderMenu();
    break;
  case SCREEN_FOCUS_TIMER:
    renderFocusTimer();
    break;
  case SCREEN_PC_METRICS:
    renderPCMetrics();
    break;
  case SCREEN_TASKS:
    renderTasks();
    break;
  }

  display.display();
  delay(15);
}
