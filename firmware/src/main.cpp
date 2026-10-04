#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

// ==========================================
// PIN DEFINITIONS FOR ESP32-C3 SUPERMINI
// ==========================================
constexpr int I2C_SDA_PIN = 4;
constexpr int I2C_SCL_PIN = 5;
constexpr int ONBOARD_LED_PIN = 8; // SuperMini blue LED (Active LOW)

// Rotary Encoder Pins
constexpr int ENCODER_CLK_PIN = 0; // Quadrature A
constexpr int ENCODER_DT_PIN = 1;  // Quadrature B
constexpr int ENCODER_SW_PIN = 3;  // Push switch (Active LOW)

// Display Calibrated Geometry
constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int SCREEN_Y_OFFSET = 22; // Skip unlit top blocks (Y = 0..21)
constexpr int USABLE_HEIGHT = 42;   // Active height: 64 - 22 = 42 pixels

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RTC_DS3231 rtc;
bool rtcFound = false;

// ==========================================
// APP STATE & SCREENS
// ==========================================
enum ScreenState {
  SCREEN_MENU,
  SCREEN_FOCUS_TIMER,
  SCREEN_PC_METRICS,
  SCREEN_TASKS
};

ScreenState currentScreen = SCREEN_MENU;

const char *menuItems[] = {"1. Focus Timer", "2. PC Metrics", "3. Tasks (Sync)",
                           "4. Device Info"};
constexpr int TOTAL_MENU_ITEMS = sizeof(menuItems) / sizeof(menuItems[0]);
int selectedMenuIndex = 0;

bool timerRunning = false;
int focusMinutes = 25;
uint32_t timerEndEpoch = 0;
uint32_t timerRemainingSeconds = 25 * 60;

void setup() {
  pinMode(ONBOARD_LED_PIN, OUTPUT);
  digitalWrite(ONBOARD_LED_PIN, HIGH);

  // Setup Rotary Encoder Pins with Internal Pull-Ups
  pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] SENU ESP32-C3 Starting...");

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  bool displayReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!displayReady) {
    display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }

  display.clearDisplay();
  display.display();

  // Initialize DS3231 RTC
  if (rtc.begin()) {
    rtcFound = true;
    Serial.println("[RTC] DS3231 detected successfully!");
    if (rtc.lostPower()) {
      Serial.println("[RTC] Battery lost power! Setting time to compile time.");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  } else {
    Serial.println(
        "[RTC] Warning: DS3231 not found on I2C bus (offline mode).");
  }
}

void renderMenu() {
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT,
                   SSD1306_WHITE);

  // Header Title at Y = 24 with Live Real-World Time & Pin Telemetry
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 24);

  // Cached RTC query (every 500ms) to keep I2C bus fast and responsive
  static unsigned long lastRtcRead = 0;
  static DateTime cachedNow;
  if (rtcFound && (millis() - lastRtcRead > 500 || lastRtcRead == 0)) {
    cachedNow = rtc.now();
    lastRtcRead = millis();
  }

  if (rtcFound) {
    display.printf("%02d:%02d:%02d", cachedNow.hour(), cachedNow.minute(),
                   cachedNow.second());
  } else {
    display.print("SENU MENU");
  }

  // Live Hardware Diagnostic: M:[Index] P:[CLK][DT][SW]
  display.setCursor(60, 24);
  display.printf("M:%d P:%d%d%d", selectedMenuIndex,
                 digitalRead(ENCODER_CLK_PIN), digitalRead(ENCODER_DT_PIN),
                 digitalRead(ENCODER_SW_PIN));

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
  display.print(timerRunning ? "FOCUS ACTIVE (POMO)" : "FOCUS PAUSED (POMO)");
  display.drawLine(2, 33, 125, 33, SSD1306_WHITE);

  // Compute remaining time from RTC epoch
  if (timerRunning && rtcFound) {
    int32_t diff = (int32_t)(timerEndEpoch - rtc.now().unixtime());
    if (diff <= 0) {
      timerRemainingSeconds = 0;
      timerRunning = false; // Session finished!
    } else {
      timerRemainingSeconds = diff;
    }
  }

  int mins = timerRemainingSeconds / 60;
  int secs = timerRemainingSeconds % 60;

  // Large timer readout
  display.setTextSize(2);
  display.setCursor(34, 36);
  display.printf("%02d:%02d", mins, secs);

  display.setTextSize(1);
  display.setCursor(6, 53);
  display.print(timerRunning ? "[Click:Pause Hld:Back]"
                             : "[Click:Start Hld:Back]");
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
  static unsigned long buttonPressStart = 0;
  static unsigned long lastButtonCheck = 0;
  static unsigned long lastClockTick = 0;
  static bool buttonWasPressed = false;
  static int lastButtonState = HIGH;
  static bool ledState = false;
  static bool displayNeedsUpdate = true;

  // 16-State Quadrature Transition Table
  // State: [Old_CLK, Old_DT, New_CLK, New_DT]
  static const int8_t qeTable[16] = {0,  -1, 1, 0, 1, 0, 0,  -1,
                                     -1, 0,  0, 1, 0, 1, -1, 0};
  static uint8_t encoderHistory = 0;
  static int8_t accumulatedSteps = 0;

  unsigned long now = millis();

  // -------------------------------------------------------------
  // 1. Universal Quadrature Decoder (Full & Half Cycle Immune)
  // -------------------------------------------------------------
  uint8_t currentPins =
      (digitalRead(ENCODER_CLK_PIN) << 1) | digitalRead(ENCODER_DT_PIN);
  uint8_t lastPins = encoderHistory & 0x03;

  if (currentPins != lastPins) {
    encoderHistory = ((encoderHistory << 2) | currentPins) & 0x0F;
    int8_t step = qeTable[encoderHistory];

    if (step != 0) {
      accumulatedSteps += step;

      // 2 or 4 state steps per physical notch
      if (accumulatedSteps >= 2 || accumulatedSteps <= -2) {
        int delta = (accumulatedSteps > 0) ? 1 : -1;
        accumulatedSteps = 0;

        if (currentScreen == SCREEN_MENU) {
          selectedMenuIndex = (selectedMenuIndex + delta) % TOTAL_MENU_ITEMS;
          if (selectedMenuIndex < 0)
            selectedMenuIndex += TOTAL_MENU_ITEMS;
          Serial.printf("[ENC] Menu Scroll -> Index: %d\n", selectedMenuIndex);
          displayNeedsUpdate = true;
        } else if (currentScreen == SCREEN_FOCUS_TIMER && !timerRunning) {
          focusMinutes += delta * 5;
          if (focusMinutes < 5)
            focusMinutes = 5;
          if (focusMinutes > 90)
            focusMinutes = 90;
          timerRemainingSeconds = focusMinutes * 60;
          displayNeedsUpdate = true;
        }
      }
    }
  }

  // -------------------------------------------------------------
  // 2. Process Encoder Pushbutton (SW) with Short/Long Press
  // -------------------------------------------------------------
  int currentButtonState = digitalRead(ENCODER_SW_PIN);

  // Press down detected (150ms debounce lockout)
  if (currentButtonState == LOW && lastButtonState == HIGH &&
      (now - lastButtonCheck > 150)) {
    buttonPressStart = now;
    buttonWasPressed = true;
    lastButtonCheck = now;
    displayNeedsUpdate = true;
  }

  // Long press (held for >= 600ms): Return to Main Menu immediately
  if (buttonWasPressed && currentButtonState == LOW &&
      (now - buttonPressStart >= 600)) {
    buttonWasPressed = false;
    ledState = !ledState;
    digitalWrite(ONBOARD_LED_PIN, ledState ? LOW : HIGH);
    currentScreen = SCREEN_MENU;
    Serial.println("[BTN] Long press -> Returned to MENU");
    displayNeedsUpdate = true;
  }

  // Short click (released before 600ms)
  if (buttonWasPressed && currentButtonState == HIGH &&
      lastButtonState == LOW) {
    buttonWasPressed = false;
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
      Serial.printf("[BTN] Short click -> Screen: %d\n", currentScreen);
    } else if (currentScreen == SCREEN_FOCUS_TIMER) {
      timerRunning = !timerRunning;
      if (timerRunning && rtcFound) {
        timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
      }
      Serial.printf("[BTN] Timer toggled -> Running: %d\n", timerRunning);
    } else {
      currentScreen = SCREEN_MENU;
    }
    displayNeedsUpdate = true;
  }
  lastButtonState = currentButtonState;

  // Refresh clock every 500ms
  if (now - lastClockTick >= 500) {
    lastClockTick = now;
    displayNeedsUpdate = true;
  }

  // -------------------------------------------------------------
  // 3. Render Active Screen (Only when state changes or on clock tick)
  // -------------------------------------------------------------
  if (displayNeedsUpdate) {
    displayNeedsUpdate = false;
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
  }
}
