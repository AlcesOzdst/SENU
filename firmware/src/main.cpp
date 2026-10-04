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
constexpr int ENCODER_SW_PIN = 3;  // Encoder built-in push switch (Active LOW)

// Experimental Hardware Pins (Branch: experiments)
constexpr int BUZZER_PIN = 6;      // Piezo/Active Buzzer Output (Pin 10 on right header)
constexpr int PAUSE_BTN_PIN = 7;   // External Tactile Push Button (Active LOW, Pin 11 on right header)

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
  SCREEN_ALARM,
  SCREEN_PC_METRICS,
  SCREEN_TASKS
};

ScreenState currentScreen = SCREEN_MENU;

const char *menuItems[] = {
  "1. Focus Timer",
  "2. Alarm Setup",
  "3. PC Metrics",
  "4. Tasks (Sync)"
};
constexpr int TOTAL_MENU_ITEMS = sizeof(menuItems) / sizeof(menuItems[0]);
int selectedMenuIndex = 0;

// Focus Timer State (Supports 1-minute sessions!)
bool timerRunning = false;
int focusMinutes = 25; // Dialable from 1 min to 90 min
uint32_t timerEndEpoch = 0;
uint32_t timerRemainingSeconds = 25 * 60;

// Alarm State (Dialable to any minute, default 06:00 AM)
int alarmHour = 6;
int alarmMinute = 0;
bool alarmEnabled = true;
bool alarmTriggered = false;
unsigned long alarmTriggerStartTime = 0;

// ==========================================
// BIZARRE BUZZER & ALARM CADENCE (Non-Blocking)
// ==========================================
void triggerAlarmBeep(unsigned long now) {
  if (alarmTriggered) {
    // BIZARRE ALARM CADENCE: 1.2-second repeating cycle
    // Chaotic sci-fi alien machine-gun stutter + syncopated wild triplets
    unsigned long phase = now % 1200;
    bool beep = false;

    if (phase < 200) {
      // 1. Rapid laser stutter (5 ultra-fast 20ms machine-gun chirps)
      beep = ((phase / 20) % 2) == 0;
    } else if (phase >= 250 && phase < 450) {
      // 2. Syncopated wild triplet: long-short-long
      beep = (phase < 330) || (phase >= 360 && phase < 390) || (phase >= 415);
    } else if (phase >= 500 && phase < 650) {
      // 3. Frantic heartbeat double-thump
      beep = (phase < 560) || (phase >= 590 && phase < 640);
    } else if (phase >= 720 && phase < 840) {
      // 4. Glitch micro-burst (12ms pulses)
      beep = ((phase / 12) % 2) == 0;
    }
    // 840ms to 1200ms: Tense quiet pause before restarting

    digitalWrite(BUZZER_PIN, beep ? HIGH : LOW);
    digitalWrite(ONBOARD_LED_PIN, beep ? LOW : HIGH); // Synchronized frantic LED
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void soundShortChirp() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(12);
  digitalWrite(BUZZER_PIN, LOW);
}

void setup() {
  pinMode(ONBOARD_LED_PIN, OUTPUT);
  digitalWrite(ONBOARD_LED_PIN, HIGH);

  // Setup Rotary Encoder Pins
  pinMode(ENCODER_CLK_PIN, INPUT_PULLUP);
  pinMode(ENCODER_DT_PIN, INPUT_PULLUP);
  pinMode(ENCODER_SW_PIN, INPUT_PULLUP);

  // Setup Experimental Hardware Pins
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(PAUSE_BTN_PIN, INPUT_PULLUP); // External tactile push button to GND

  Serial.begin(115200);
  delay(500);
  Serial.println("\n[BOOT] SENU ESP32-C3 Starting (experiments branch)...");
  Serial.printf("[ALARM] Initialized for %02d:%02d\n", alarmHour, alarmMinute);
  Serial.println("[PINS] Buzzer -> GPIO 6 | Tactile Push Button -> GPIO 7");

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
    Serial.println("[RTC] Warning: DS3231 not found on I2C bus (offline fallback).");
  }

  // Startup confirmation chirp
  soundShortChirp();
}

void renderMenu() {
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT, SSD1306_WHITE);

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
    display.printf("%02d:%02d:%02d", cachedNow.hour(), cachedNow.minute(), cachedNow.second());
  } else {
    display.print("SENU MENU");
  }

  // Live Hardware Diagnostic: M:[Index] P:[CLK][DT][SW][Btn]
  display.setCursor(56, 24);
  display.printf("M:%d P:%d%d%d%d", selectedMenuIndex,
                 digitalRead(ENCODER_CLK_PIN), digitalRead(ENCODER_DT_PIN),
                 digitalRead(ENCODER_SW_PIN), digitalRead(PAUSE_BTN_PIN));

  display.drawLine(2, 33, 125, 33, SSD1306_WHITE);

  // Calculate visible slice (show 2 items at a time inside the 42px window)
  int startIndex = (selectedMenuIndex >= 2) ? selectedMenuIndex - 1 : 0;
  if (startIndex > TOTAL_MENU_ITEMS - 2)
    startIndex = TOTAL_MENU_ITEMS - 2;

  for (int i = 0; i < 2; ++i) {
    int itemIdx = startIndex + i;
    int yPos = 36 + (i * 12);

    if (itemIdx == selectedMenuIndex) {
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
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 24);
  if (alarmTriggered) {
    display.print("*** TIME'S UP! ***");
  } else if (timerRunning) {
    display.print("FOCUS ACTIVE (POMO)");
  } else if (timerRemainingSeconds < (uint32_t)(focusMinutes * 60)) {
    display.print("FOCUS PAUSED (POMO)");
  } else {
    display.print("SET FOCUS DURATION");
  }
  display.drawLine(2, 33, 125, 33, SSD1306_WHITE);

  int mins = timerRemainingSeconds / 60;
  int secs = timerRemainingSeconds % 60;

  display.setTextSize(2);
  display.setCursor(34, 36);
  display.printf("%02d:%02d", mins, secs);

  display.setTextSize(1);
  display.setCursor(4, 53);
  if (alarmTriggered) {
    display.print("[Push Btn to Mute]");
  } else if (timerRunning) {
    display.print("[Btn:Pause Hold:Menu]");
  } else if (timerRemainingSeconds < (uint32_t)(focusMinutes * 60)) {
    display.print("[Btn:Resume Clk:Start]");
  } else {
    display.print("[Dial:Min  Clk:Start]");
  }
}

void renderAlarmScreen() {
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 24);
  display.printf("ALARM: %02d:%02d", alarmHour, alarmMinute);
  display.drawLine(2, 33, 125, 33, SSD1306_WHITE);

  if (alarmTriggered) {
    // Flashing Alert Header
    display.fillRect(4, 36, 120, 14, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setCursor(14, 39);
    display.print("! WAKE UP !");

    display.setTextColor(SSD1306_WHITE);
    display.setCursor(6, 53);
    display.print("[Push Btn: Snooze]");
  } else {
    display.setCursor(6, 37);
    display.printf("Status: %s", alarmEnabled ? "ENABLED (ON)" : "DISABLED (OFF)");

    display.setCursor(6, 52);
    display.print("[Dial:Min Btn:Test]");
  }
}

void renderPCMetrics() {
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT, SSD1306_WHITE);

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
  display.drawRect(0, SCREEN_Y_OFFSET, SCREEN_WIDTH, USABLE_HEIGHT, SSD1306_WHITE);

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
  static unsigned long lastTactileCheck = 0;
  static unsigned long lastClockTick = 0;
  static int lastTactileState = HIGH;
  static bool buttonWasPressed = false;
  static int lastButtonState = HIGH;
  static bool ledState = false;
  static bool displayNeedsUpdate = true;

  // 16-State Quadrature Transition Table
  static const int8_t qeTable[16] = {
      0,  -1,  1,  0,
      1,   0,  0, -1,
     -1,   0,  0,  1,
      0,   1, -1,  0
  };
  static uint8_t encoderHistory = 0;
  static int8_t accumulatedSteps = 0;

  unsigned long now = millis();

  // -------------------------------------------------------------
  // 0. Update Bizarre Alarm Buzzer Audio & Flashing LED
  // -------------------------------------------------------------
  triggerAlarmBeep(now);

  // -------------------------------------------------------------
  // 1. Check Real-Time Clock Alarm (Matches exact minute)
  // -------------------------------------------------------------
  static unsigned long lastAlarmCheck = 0;
  if (rtcFound && (now - lastAlarmCheck >= 1000)) {
    lastAlarmCheck = now;
    DateTime currentTime = rtc.now();

    // Check if current time matches scheduled alarm
    if (alarmEnabled && !alarmTriggered) {
      if (currentTime.hour() == alarmHour && currentTime.minute() == alarmMinute && currentTime.second() == 0) {
        alarmTriggered = true;
        alarmTriggerStartTime = now;
        currentScreen = SCREEN_ALARM;
        displayNeedsUpdate = true;
        Serial.printf("[ALARM] >>> BIZARRE ALARM TRIGGERED at %02d:%02d! <<<\n", alarmHour, alarmMinute);
      }
    }

    // Auto-silence after 3 minutes if untouched
    if (alarmTriggered && (now - alarmTriggerStartTime > 180000)) {
      alarmTriggered = false;
      displayNeedsUpdate = true;
    }
  }

  // -------------------------------------------------------------
  // 1b. Check Focus Timer Countdown (Global non-blocking timer)
  // -------------------------------------------------------------
  if (timerRunning) {
    if (rtcFound) {
      int32_t diff = (int32_t)(timerEndEpoch - rtc.now().unixtime());
      if (diff <= 0) {
        timerRemainingSeconds = 0;
        timerRunning = false;
        alarmTriggered = true;
        alarmTriggerStartTime = now;
        currentScreen = SCREEN_FOCUS_TIMER; // Switch to timer screen when complete
        displayNeedsUpdate = true;
        Serial.println("[POMO] Session COMPLETE! Bizarre alarm triggered!");
      } else {
        if (timerRemainingSeconds != (uint32_t)diff) {
          timerRemainingSeconds = diff;
          if (currentScreen == SCREEN_FOCUS_TIMER) {
            displayNeedsUpdate = true;
          }
        }
      }
    } else {
      static unsigned long lastTimerTick = 0;
      if (now - lastTimerTick >= 1000) {
        lastTimerTick = now;
        if (timerRemainingSeconds > 0) {
          timerRemainingSeconds--;
        }
        if (timerRemainingSeconds == 0) {
          timerRunning = false;
          alarmTriggered = true;
          alarmTriggerStartTime = now;
          currentScreen = SCREEN_FOCUS_TIMER;
          displayNeedsUpdate = true;
          Serial.println("[POMO] Session COMPLETE! Bizarre alarm triggered!");
        } else if (currentScreen == SCREEN_FOCUS_TIMER) {
          displayNeedsUpdate = true;
        }
      }
    }
  }

  // -------------------------------------------------------------
  // 2. Dedicated Tactile Push Button (GPIO 7) - Pause / Snooze / Test
  // -------------------------------------------------------------
  int currentTactileState = digitalRead(PAUSE_BTN_PIN);
  if (currentTactileState == LOW && lastTactileState == HIGH && (now - lastTactileCheck > 150)) {
    lastTactileCheck = now;
    soundShortChirp();

    if (alarmTriggered) {
      // Tactile button DISMISSES / SNOOZES the active alarm!
      alarmTriggered = false;
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("[TACTILE_BTN] Alarm dismissed/snoozed!");
      displayNeedsUpdate = true;
    } else if (currentScreen == SCREEN_FOCUS_TIMER) {
      // Tactile button PAUSES / RESUMES the Pomodoro timer!
      timerRunning = !timerRunning;
      if (timerRunning) {
        if (timerRemainingSeconds == 0) {
          timerRemainingSeconds = focusMinutes * 60;
        }
        if (rtcFound) {
          timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
        }
      }
      Serial.printf("[TACTILE_BTN] Focus Timer -> Running: %d\n", timerRunning);
      displayNeedsUpdate = true;
    } else if (currentScreen == SCREEN_ALARM) {
      // In Alarm screen: Pressing tactile button test-fires the bizarre alarm!
      alarmTriggered = true;
      alarmTriggerStartTime = now;
      Serial.println("[ALARM] Test-firing bizarre buzzer!");
      displayNeedsUpdate = true;
    } else {
      // Quick tactile feedback
      ledState = !ledState;
      digitalWrite(ONBOARD_LED_PIN, ledState ? LOW : HIGH);
      Serial.println("[TACTILE_BTN] Button pressed!");
      displayNeedsUpdate = true;
    }
  }
  lastTactileState = currentTactileState;

  // -------------------------------------------------------------
  // 3. Universal Quadrature Rotary Encoder Decoder
  // -------------------------------------------------------------
  uint8_t currentPins = (digitalRead(ENCODER_CLK_PIN) << 1) | digitalRead(ENCODER_DT_PIN);
  uint8_t lastPins = encoderHistory & 0x03;

  if (currentPins != lastPins) {
    encoderHistory = ((encoderHistory << 2) | currentPins) & 0x0F;
    int8_t step = qeTable[encoderHistory];

    if (step != 0) {
      accumulatedSteps += step;

      // Filter detent notches cleanly (accumulates 2 quadrature transitions per click)
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
          // 1-minute steps! Dial all the way down to 1 minute!
          focusMinutes += delta;
          if (focusMinutes < 1) focusMinutes = 1;
          if (focusMinutes > 90) focusMinutes = 90;
          timerRemainingSeconds = focusMinutes * 60;
          Serial.printf("[POMO] Focus set to: %d min\n", focusMinutes);
          displayNeedsUpdate = true;
        } else if (currentScreen == SCREEN_ALARM) {
          // Dial alarm in 1-minute steps (00:00 to 23:59)
          int totalAlarmMins = alarmHour * 60 + alarmMinute;
          totalAlarmMins = (totalAlarmMins + delta + 1440) % 1440;
          alarmHour = totalAlarmMins / 60;
          alarmMinute = totalAlarmMins % 60;
          Serial.printf("[ALARM] Set to %02d:%02d\n", alarmHour, alarmMinute);
          displayNeedsUpdate = true;
        }
      }
    }
  }

  // -------------------------------------------------------------
  // 4. Encoder Pushbutton (SW) with Short/Long Press
  // -------------------------------------------------------------
  int currentButtonState = digitalRead(ENCODER_SW_PIN);

  if (currentButtonState == LOW && lastButtonState == HIGH && (now - lastButtonCheck > 150)) {
    buttonPressStart = now;
    buttonWasPressed = true;
    lastButtonCheck = now;
    displayNeedsUpdate = true;
  }

  // Long press (held for >= 600ms): Return to Main Menu immediately
  if (buttonWasPressed && currentButtonState == LOW && (now - buttonPressStart >= 600)) {
    buttonWasPressed = false;
    ledState = !ledState;
    digitalWrite(ONBOARD_LED_PIN, ledState ? LOW : HIGH);
    currentScreen = SCREEN_MENU;
    Serial.println("[BTN] Long press -> Returned to MENU");
    displayNeedsUpdate = true;
  }

  // Short click (released before 600ms)
  if (buttonWasPressed && currentButtonState == HIGH && lastButtonState == LOW) {
    buttonWasPressed = false;
    ledState = !ledState;
    digitalWrite(ONBOARD_LED_PIN, ledState ? LOW : HIGH);

    if (alarmTriggered) {
      // Clicking also silences the alarm
      alarmTriggered = false;
      digitalWrite(BUZZER_PIN, LOW);
      displayNeedsUpdate = true;
    } else if (currentScreen == SCREEN_MENU) {
      if (selectedMenuIndex == 0)
        currentScreen = SCREEN_FOCUS_TIMER;
      else if (selectedMenuIndex == 1)
        currentScreen = SCREEN_ALARM;
      else if (selectedMenuIndex == 2)
        currentScreen = SCREEN_PC_METRICS;
      else if (selectedMenuIndex == 3)
        currentScreen = SCREEN_TASKS;
      else
        currentScreen = SCREEN_MENU;
      Serial.printf("[BTN] Selected Screen: %d\n", currentScreen);
    } else if (currentScreen == SCREEN_FOCUS_TIMER) {
      timerRunning = !timerRunning;
      if (timerRunning) {
        if (timerRemainingSeconds == 0) {
          timerRemainingSeconds = focusMinutes * 60;
        }
        if (rtcFound) {
          timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
        }
      }
      Serial.printf("[BTN] Focus Timer toggled -> Running: %d\n", timerRunning);
    } else if (currentScreen == SCREEN_ALARM) {
      // Toggle alarm enabled / disabled
      alarmEnabled = !alarmEnabled;
      Serial.printf("[ALARM] Enabled: %d\n", alarmEnabled);
    } else {
      currentScreen = SCREEN_MENU;
    }
    displayNeedsUpdate = true;
  }
  lastButtonState = currentButtonState;

  // Refresh clock/display every 500ms
  if (now - lastClockTick >= 500) {
    lastClockTick = now;
    displayNeedsUpdate = true;
  }

  // -------------------------------------------------------------
  // 5. Render Active Screen
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
    case SCREEN_ALARM:
      renderAlarmScreen();
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
