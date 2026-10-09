#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>
#include "config.h"

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

// Display Calibrated Geometry (New 128x64 OLED Panel)
constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int SCREEN_Y_OFFSET = 0;   // Full 128x64 active panel (Y = 0..63)
constexpr int USABLE_HEIGHT = 64;    // Full 64px display canvas

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RTC_DS3231 rtc;
bool rtcFound = false;
bool displayNeedsUpdate = true;

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

// Alarm Field Selection (Toggled via Tactile Button)
enum AlarmEditField {
  EDIT_HOURS,
  EDIT_MINUTES
};
AlarmEditField currentAlarmField = EDIT_HOURS;

// Snooze Management
bool snoozeActive = false;
uint32_t snoozeUntilEpoch = 0;

// ==========================================
// NETWORK & PC METRICS (Milestone M3)
// ==========================================
enum NetworkStatus {
  NET_IDLE,
  NET_CONNECTING,
  NET_CONNECTED,
  NET_AGENT_ONLINE,
  NET_AGENT_OFFLINE
};

volatile NetworkStatus netStatus = NET_IDLE;
volatile float pcCpuPercent = 0.0f;
volatile float pcMemoryPercent = 0.0f;
volatile uint64_t pcMemoryUsedBytes = 0;
volatile uint64_t pcMemoryTotalBytes = 0;
volatile unsigned long lastMetricsFetchTime = 0;
volatile bool netForceRefresh = false;

char wifiSsid[64] = WIFI_SSID;
char wifiPass[64] = WIFI_PASS;
char agentHost[64] = AGENT_HOST;
int agentPort = AGENT_PORT;
TaskHandle_t netTaskHandle = NULL;

void networkTask(void *pvParameters) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPass);
  netStatus = NET_CONNECTING;
  Serial.printf("[NET] Connecting to Wi-Fi SSID: '%s'...\n", wifiSsid);

  HTTPClient http;
  WiFiClient client;
  http.setTimeout(1500);

  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      if (netStatus == NET_CONNECTING || netStatus == NET_IDLE) {
        netStatus = NET_CONNECTED;
        Serial.printf("[NET] Wi-Fi Connected! IP: %s | RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        displayNeedsUpdate = true;
      }

      char url[128];
      snprintf(url, sizeof(url), "http://%s:%d/metrics/system", agentHost, agentPort);
      http.begin(client, url);

      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
          pcCpuPercent = doc["cpu_percent"] | 0.0f;
          pcMemoryPercent = doc["memory_percent"] | 0.0f;
          pcMemoryUsedBytes = doc["memory_used_bytes"] | 0ULL;
          pcMemoryTotalBytes = doc["memory_total_bytes"] | 0ULL;
          lastMetricsFetchTime = millis();
          netStatus = NET_AGENT_ONLINE;
          displayNeedsUpdate = true;
        }
      } else {
        if (netStatus != NET_AGENT_OFFLINE) {
          Serial.printf("[NET] Agent offline or returned HTTP %d at %s\n", httpCode, url);
        }
        netStatus = NET_AGENT_OFFLINE;
        displayNeedsUpdate = true;
      }
      http.end();
    } else {
      if (netStatus != NET_CONNECTING) {
        netStatus = NET_IDLE;
        WiFi.begin(wifiSsid, wifiPass);
        netStatus = NET_CONNECTING;
      }
    }

    int slices = METRICS_POLL_INTERVAL_MS / 100;
    for (int i = 0; i < slices; i++) {
      if (netForceRefresh) {
        netForceRefresh = false;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

// ==========================================
// HARRY POTTER / HEDWIG'S THEME (Melody Engine)
// ==========================================
#define NOTE_AS4  466
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_CS5  554
#define NOTE_D5   587
#define NOTE_DS5  622
#define NOTE_E5   659
#define NOTE_F5   698
#define NOTE_FS5  740
#define NOTE_G5   784
#define NOTE_GS5  831
#define NOTE_A5   880
#define NOTE_AS5  932
#define NOTE_B5   988
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245
#define NOTE_E6  1319
#define REST        0

struct NoteStep {
  uint16_t freq;
  uint16_t duration;
  uint16_t pause;
};

// Iconic Hedwig's Theme (Harry Potter main motif)
const NoteStep hedwigTheme[] = {
  // Opening motif
  {NOTE_B4,  350,  60},
  {NOTE_E5,  520,  60},
  {NOTE_G5,  200,  60},
  {NOTE_FS5, 350,  60},
  {NOTE_E5,  700,  60},
  {NOTE_B5,  350,  60},
  {NOTE_A5,  850, 100},
  {NOTE_FS5, 850, 100},

  // Second phrase
  {NOTE_E5,  520,  60},
  {NOTE_G5,  200,  60},
  {NOTE_FS5, 350,  60},
  {NOTE_DS5, 700,  60},
  {NOTE_F5,  350,  60},
  {NOTE_B4, 1000, 150},

  // Third phrase
  {NOTE_B4,  350,  60},
  {NOTE_E5,  520,  60},
  {NOTE_G5,  200,  60},
  {NOTE_FS5, 350,  60},
  {NOTE_E5,  700,  60},
  {NOTE_B5,  350,  60},
  {NOTE_D6,  700,  60},
  {NOTE_CS6, 350,  60},
  {NOTE_C6,  700,  60},
  {NOTE_GS5, 350,  60},

  // Fourth phrase (cadence)
  {NOTE_C6,  520,  60},
  {NOTE_B5,  200,  60},
  {NOTE_AS5, 350,  60},
  {NOTE_AS4, 700,  60},
  {NOTE_G5,  350,  60},
  {NOTE_E5, 1100, 400}
};
constexpr int HEDWIG_MELODY_LEN = sizeof(hedwigTheme) / sizeof(hedwigTheme[0]);

int melodyNoteIndex = 0;
unsigned long melodyNoteStartTime = 0;
bool melodySounding = false;

void updateMelodyPlayer(unsigned long now) {
  if (!alarmTriggered) {
    if (melodySounding) {
      noTone(BUZZER_PIN);
      digitalWrite(ONBOARD_LED_PIN, HIGH);
      melodySounding = false;
    }
    melodyNoteIndex = 0;
    melodyNoteStartTime = 0;
    return;
  }

  // Start initial note
  if (melodyNoteStartTime == 0) {
    melodyNoteStartTime = now;
    melodySounding = true;
    if (hedwigTheme[melodyNoteIndex].freq > 0) {
      tone(BUZZER_PIN, hedwigTheme[melodyNoteIndex].freq);
      digitalWrite(ONBOARD_LED_PIN, LOW); // Flash LED with note
    } else {
      noTone(BUZZER_PIN);
      digitalWrite(ONBOARD_LED_PIN, HIGH);
    }
    return;
  }

  unsigned long elapsed = now - melodyNoteStartTime;
  uint16_t dur = hedwigTheme[melodyNoteIndex].duration;
  uint16_t pause = hedwigTheme[melodyNoteIndex].pause;

  // Finished note, enter inter-note pause
  if (melodySounding && elapsed >= dur) {
    noTone(BUZZER_PIN);
    digitalWrite(ONBOARD_LED_PIN, HIGH);
    melodySounding = false;
  }

  // Finished note + pause, advance to next note in melody
  if (elapsed >= (dur + pause)) {
    melodyNoteIndex = (melodyNoteIndex + 1) % HEDWIG_MELODY_LEN;
    melodyNoteStartTime = now;
    melodySounding = true;
    if (hedwigTheme[melodyNoteIndex].freq > 0) {
      tone(BUZZER_PIN, hedwigTheme[melodyNoteIndex].freq);
      digitalWrite(ONBOARD_LED_PIN, LOW);
    } else {
      noTone(BUZZER_PIN);
      digitalWrite(ONBOARD_LED_PIN, HIGH);
    }
  }
}

void soundShortChirp() {
  tone(BUZZER_PIN, 1800, 20);
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
  Serial.printf("[PINS] GPIO 7 level at boot: %d (1 = Open/HIGH, 0 = Pressed/LOW)\n", digitalRead(PAUSE_BTN_PIN));

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
    
    // Automatically sync RTC to your PC's exact time whenever uploaded!
    DateTime compileTime = DateTime(F(__DATE__), F(__TIME__)) + TimeSpan(0, 0, 0, 5);
    rtc.adjust(compileTime);
    Serial.printf("[RTC] Synced RTC to PC clock: %02d:%02d:%02d\n",
                  compileTime.hour(), compileTime.minute(), compileTime.second());
  } else {
    Serial.println("[RTC] Warning: DS3231 not found on I2C bus (offline fallback).");
  }

  // Launch Background Network Task for Wi-Fi & PC Metrics (FreeRTOS)
  xTaskCreate(networkTask, "senu_net", 6144, NULL, 1, &netTaskHandle);
  Serial.println("[NET] Background Wi-Fi & PC metrics polling task launched!");

  // Startup confirmation chirp
  soundShortChirp();
}

void renderMenu() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  // Header Title at Y = 3 with Live Real-World Time & Pin Telemetry
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 3);

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
  display.setCursor(56, 3);
  display.printf("M:%d P:%d%d%d%d", selectedMenuIndex,
                 digitalRead(ENCODER_CLK_PIN), digitalRead(ENCODER_DT_PIN),
                 digitalRead(ENCODER_SW_PIN), digitalRead(PAUSE_BTN_PIN));

  // Live network badge on top-right of header (X = 123, Y = 6)
  if (netStatus == NET_AGENT_ONLINE) {
    display.fillCircle(123, 6, 2, SSD1306_WHITE); // Solid dot: Agent Online
  } else if (WiFi.status() == WL_CONNECTED) {
    display.drawCircle(123, 6, 2, SSD1306_WHITE); // Hollow circle: Wi-Fi connected, agent offline
  }

  display.drawLine(2, 12, 125, 12, SSD1306_WHITE);

  // Render all 4 items simultaneously on the full 128x64 display!
  for (int i = 0; i < TOTAL_MENU_ITEMS; ++i) {
    int yPos = 14 + (i * 12);

    if (i == selectedMenuIndex) {
      display.fillRect(2, yPos, 122, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.setCursor(6, yPos + 2);
      display.printf("> %s", menuItems[i]);
    } else {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(6, yPos + 2);
      display.printf("  %s", menuItems[i]);
    }
  }

  // Scroll indicator notch on the right edge
  int scrollY = 14 + (selectedMenuIndex * 12);
  display.fillRect(124, scrollY, 2, 11, SSD1306_WHITE);
}

void renderFocusTimer() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 3);
  if (alarmTriggered) {
    display.print("*** TIME'S UP! ***");
  } else if (timerRunning) {
    display.print("FOCUS ACTIVE (POMO)");
  } else if (timerRemainingSeconds < (uint32_t)(focusMinutes * 60)) {
    display.print("FOCUS PAUSED (POMO)");
  } else {
    display.print("SET FOCUS DURATION");
  }

  // Mini session indicator on right of header
  display.setCursor(102, 3);
  display.printf("%2dm", focusMinutes);

  display.drawLine(2, 12, 125, 12, SSD1306_WHITE);

  int mins = timerRemainingSeconds / 60;
  int secs = timerRemainingSeconds % 60;

  // Prominent Centered Large Countdown Digits
  display.setTextSize(3);
  display.setCursor(19, 17);
  display.printf("%02d:%02d", mins, secs);

  // Focus Session Progress Bar
  uint32_t totalSec = focusMinutes * 60;
  uint32_t elapsed = (totalSec > timerRemainingSeconds) ? (totalSec - timerRemainingSeconds) : 0;
  display.drawRect(14, 43, 100, 6, SSD1306_WHITE);
  if (totalSec > 0) {
    int progW = map(elapsed, 0, totalSec, 0, 96);
    if (progW > 0) {
      display.fillRect(16, 45, progW, 2, SSD1306_WHITE);
    }
  }

  display.setTextSize(1);
  display.setCursor(4, 53);
  if (alarmTriggered) {
    display.print("[Btn:Mute  Hold:Snooze]");
  } else if (timerRunning) {
    display.print("[Btn:Pause  Hold:Reset]");
  } else if (timerRemainingSeconds < (uint32_t)(focusMinutes * 60)) {
    display.print("[Btn:Resume Clk:Start]");
  } else {
    display.print("[Dial:Min   Clk:Start]");
  }
}

void renderAlarmScreen() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 3);
  if (alarmTriggered) {
    display.print("ALARM: HEDWIG THEME!");
  } else if (snoozeActive) {
    display.print("ALARM (SNOOZED +5M)");
  } else {
    display.print("ALARM SETUP");
  }
  display.drawLine(2, 12, 125, 12, SSD1306_WHITE);

  if (alarmTriggered) {
    // Inverted Alert Header
    display.fillRect(4, 16, 120, 22, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(16, 20);
    display.print("WAKE UP!");

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(8, 42);
    display.print("Hedwig's Theme Playing");
    display.setCursor(4, 53);
    display.print("Tap:Off  Hold:Snooze");
  } else {
    // Big centered digits showing [HH]:MM or HH:[MM]
    display.setTextSize(2);
    display.setCursor(22, 18);
    if (currentAlarmField == EDIT_HOURS) {
      display.printf("[%02d]:%02d", alarmHour, alarmMinute);
    } else {
      display.printf("%02d:[%02d]", alarmHour, alarmMinute);
    }

    display.setTextSize(1);
    display.setCursor(8, 39);
    display.printf("Status: %s  Tone:Hedwig", alarmEnabled ? "ACTIVE" : "OFF");

    display.setCursor(4, 53);
    if (currentAlarmField == EDIT_HOURS) {
      display.printf("Tac:Min Knob:Hr Clk:%s", alarmEnabled ? "Off" : "On");
    } else {
      display.printf("Tac:Hr  Knob:Min Clk:%s", alarmEnabled ? "Off" : "On");
    }
  }
}

void renderPCMetrics() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 3);

  // Status Header
  if (netStatus == NET_AGENT_ONLINE) {
    display.print("HOST PC METRICS [OK]");
    display.fillCircle(123, 6, 2, SSD1306_WHITE);
  } else if (netStatus == NET_AGENT_OFFLINE) {
    display.print("PC METRICS [OFFLINE]");
    display.drawCircle(123, 6, 2, SSD1306_WHITE);
  } else if (netStatus == NET_CONNECTING) {
    display.print("PC METRICS [WIFI...]");
  } else {
    display.print("PC METRICS [NO WIFI]");
  }
  display.drawLine(2, 12, 125, 12, SSD1306_WHITE);

  if (netStatus == NET_AGENT_ONLINE) {
    // Dynamic Gauges with Outline Box & Proportional Fill Bar
    // Line 1: CPU (Y = 16)
    display.setCursor(4, 17);
    display.printf("CPU:%3.0f%%", pcCpuPercent);
    display.drawRect(54, 16, 70, 9, SSD1306_WHITE);
    int cpuBarW = map(constrain((int)pcCpuPercent, 0, 100), 0, 100, 0, 66);
    if (cpuBarW > 0) {
      display.fillRect(56, 18, cpuBarW, 5, SSD1306_WHITE);
    }

    // Line 2: RAM (Y = 28)
    display.setCursor(4, 29);
    display.printf("RAM:%3.0f%%", pcMemoryPercent);
    display.drawRect(54, 28, 70, 9, SSD1306_WHITE);
    int ramBarW = map(constrain((int)pcMemoryPercent, 0, 100), 0, 100, 0, 66);
    if (ramBarW > 0) {
      display.fillRect(56, 30, ramBarW, 5, SSD1306_WHITE);
    }

    // Line 3: Memory Telemetry (Y = 41)
    float usedGB = (float)pcMemoryUsedBytes / (1024.0f * 1024.0f * 1024.0f);
    float totGB = (float)pcMemoryTotalBytes / (1024.0f * 1024.0f * 1024.0f);
    display.setCursor(4, 41);
    display.printf("RAM: %.1fG / %.1fG", usedGB, totGB);

    // Line 4: Footer (Y = 53)
    display.setCursor(4, 53);
    unsigned long secAgo = (millis() - lastMetricsFetchTime) / 1000;
    display.printf("Live: %lus ago [Tac:Ref]", secAgo);
  } else if (netStatus == NET_AGENT_OFFLINE) {
    display.setCursor(4, 17);
    display.printf("Host: %s:%d", agentHost, agentPort);
    display.setCursor(4, 29);
    display.print("Status: Agent Offline");
    display.setCursor(4, 41);
    display.print("Run desktop_agent/run.py");
    display.setCursor(4, 53);
    display.print("Tac:Poll   Knob:Return");
  } else {
    display.setCursor(4, 17);
    display.printf("SSID: %.15s", wifiSsid);
    display.setCursor(4, 29);
    display.print("Status: Connecting Wi-Fi");
    display.setCursor(4, 41);
    display.print("Check 2.4GHz Wi-Fi AP");
    display.setCursor(4, 53);
    display.print("Tac:Poll   Knob:Return");
  }
}

void renderTasks() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 3);
  display.print("UPCOMING TASKS");
  display.drawLine(2, 12, 125, 12, SSD1306_WHITE);

  display.setCursor(6, 16);
  display.print("[ ] 1. Finish M3 Sync");

  display.setCursor(6, 28);
  display.print("[ ] 2. Test Harry Potter");

  display.setCursor(6, 40);
  display.print("[x] 3. DS3231 RTC Clock");

  display.setCursor(6, 52);
  display.print("[x] 4. New 128x64 OLED");
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
  // Live Serial Commands for Clock Adjustment (e.g. SET 01:48)
  // -------------------------------------------------------------
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    if (line.startsWith("SET ") || line.startsWith("set ")) {
      int h = -1, m = -1, s = 0;
      int parsed = sscanf(line.c_str() + 4, "%d:%d:%d", &h, &m, &s);
      if (parsed < 2) {
        parsed = sscanf(line.c_str() + 4, "%d:%d", &h, &m);
      }
      if (parsed >= 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
        if (rtcFound) {
          DateTime cur = rtc.now();
          rtc.adjust(DateTime(cur.year(), cur.month(), cur.day(), h, m, s));
          Serial.printf("[RTC] Clock successfully adjusted to %02d:%02d:%02d!\n", h, m, s);
          displayNeedsUpdate = true;
        }
      } else {
        Serial.println("[RTC] Format error! Use: SET HH:MM (e.g. SET 01:48)");
      }
    } else if (line.equalsIgnoreCase("SYNC")) {
      if (rtcFound) {
        DateTime compileTime = DateTime(F(__DATE__), F(__TIME__)) + TimeSpan(0, 0, 0, 6);
        rtc.adjust(compileTime);
        Serial.printf("[RTC] Synced to build time: %02d:%02d:%02d\n", compileTime.hour(), compileTime.minute(), compileTime.second());
        displayNeedsUpdate = true;
      }
    } else if (line.startsWith("WIFI ") || line.startsWith("wifi ")) {
      // Format: WIFI <SSID> <PASSWORD>
      int firstSpace = line.indexOf(' ');
      int secondSpace = line.indexOf(' ', firstSpace + 1);
      if (secondSpace > firstSpace) {
        String newSsid = line.substring(firstSpace + 1, secondSpace);
        String newPass = line.substring(secondSpace + 1);
        strncpy(wifiSsid, newSsid.c_str(), sizeof(wifiSsid) - 1);
        strncpy(wifiPass, newPass.c_str(), sizeof(wifiPass) - 1);
        Serial.printf("[NET] Updated Wi-Fi credentials: SSID='%s'\n", wifiSsid);
        WiFi.disconnect();
        WiFi.begin(wifiSsid, wifiPass);
        netStatus = NET_CONNECTING;
        displayNeedsUpdate = true;
      } else {
        Serial.println("[NET] Format error! Use: WIFI <ssid> <password>");
      }
    } else if (line.startsWith("HOST ") || line.startsWith("host ")) {
      // Format: HOST <IP>
      int firstSpace = line.indexOf(' ');
      String hostStr = line.substring(firstSpace + 1);
      hostStr.trim();
      strncpy(agentHost, hostStr.c_str(), sizeof(agentHost) - 1);
      Serial.printf("[NET] Updated Agent Host to: %s:%d\n", agentHost, agentPort);
      netForceRefresh = true;
      displayNeedsUpdate = true;
    } else if (line.equalsIgnoreCase("STATUS")) {
      Serial.println("================ SENU STATUS ================");
      if (rtcFound) {
        DateTime cur = rtc.now();
        Serial.printf("RTC Time: %02d:%02d:%02d (%04d-%02d-%02d)\n", cur.hour(), cur.minute(), cur.second(), cur.year(), cur.month(), cur.day());
      }
      Serial.printf("Wi-Fi Status: %s | SSID: %s\n", (WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED"), wifiSsid);
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("ESP32 IP: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
      }
      Serial.printf("Desktop Agent: http://%s:%d/metrics/system\n", agentHost, agentPort);
      Serial.printf("Agent Status: %s\n", (netStatus == NET_AGENT_ONLINE ? "ONLINE" : "OFFLINE"));
      Serial.printf("CPU: %.1f%% | RAM: %.1f%%\n", pcCpuPercent, pcMemoryPercent);
      Serial.println("=============================================");
    }
  }

  // -------------------------------------------------------------
  // 0. Update Harry Potter Theme Buzzer Melody & Flashing LED
  // -------------------------------------------------------------
  updateMelodyPlayer(now);

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
        snoozeActive = false;
        alarmTriggerStartTime = now;
        currentScreen = SCREEN_ALARM;
        displayNeedsUpdate = true;
        Serial.printf("[ALARM] >>> HARRY POTTER THEME ALARM TRIGGERED at %02d:%02d! <<<\n", alarmHour, alarmMinute);
      }
    }

    // Auto-silence after 3 minutes if untouched
    if (alarmTriggered && (now - alarmTriggerStartTime > 180000)) {
      alarmTriggered = false;
      noTone(BUZZER_PIN);
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
        Serial.println("[POMO] Session COMPLETE! Harry Potter Theme triggered!");
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
          Serial.println("[POMO] Session COMPLETE! Harry Potter Theme triggered!");
        } else if (currentScreen == SCREEN_FOCUS_TIMER) {
          displayNeedsUpdate = true;
        }
      }
    }
  }

  // -------------------------------------------------------------
  // 1c. Check Snooze Expiration (+5 Minutes)
  // -------------------------------------------------------------
  if (snoozeActive && !alarmTriggered) {
    uint32_t currentEpoch = rtcFound ? rtc.now().unixtime() : (millis() / 1000);
    if (currentEpoch >= snoozeUntilEpoch) {
      snoozeActive = false;
      alarmTriggered = true;
      alarmTriggerStartTime = now;
      currentScreen = SCREEN_ALARM;
      displayNeedsUpdate = true;
      Serial.println("[ALARM] >>> SNOOZE EXPIRED! Playing Harry Potter Theme! <<<");
    }
  }

  // -------------------------------------------------------------
  // 2. Dedicated Tactile Push Button (GPIO 7) - Short & Long Press
  // -------------------------------------------------------------
  static int tactileStableState = HIGH;
  static int tactileLastReading = HIGH;
  static unsigned long tactileLastDebounceTime = 0;
  static unsigned long tactilePressStartTime = 0;
  static bool tactileLongPressTriggered = false;

  int reading = digitalRead(PAUSE_BTN_PIN);
  if (reading != tactileLastReading) {
    tactileLastDebounceTime = now;
  }

  if ((now - tactileLastDebounceTime) > 30) {
    if (reading != tactileStableState) {
      tactileStableState = reading;
      Serial.printf("[TAC] GPIO 7 level -> %s\n", (tactileStableState == LOW) ? "PRESSED (LOW)" : "RELEASED (HIGH)");

      if (tactileStableState == LOW) {
        // Button pressed down
        tactilePressStartTime = now;
        tactileLongPressTriggered = false;
        soundShortChirp();
      } else {
        // Button released
        if (!tactileLongPressTriggered) {
          // *** SHORT PRESS ACTION ***
          Serial.println("[TAC] Short Press detected!");

          if (alarmTriggered) {
            // "just press to switch off the alarm"
            alarmTriggered = false;
            snoozeActive = false;
            noTone(BUZZER_PIN);
            digitalWrite(ONBOARD_LED_PIN, HIGH);
            Serial.println("[ALARM] Switched OFF via Tactile Button tap!");
            displayNeedsUpdate = true;
          } else if (currentScreen == SCREEN_ALARM) {
            // "When I press the tacbutton it should toggle between hours and minutes"
            currentAlarmField = (currentAlarmField == EDIT_HOURS) ? EDIT_MINUTES : EDIT_HOURS;
            Serial.printf("[ALARM] Toggle field -> Editing %s\n", (currentAlarmField == EDIT_HOURS) ? "HOURS" : "MINUTES");
            displayNeedsUpdate = true;
          } else if (currentScreen == SCREEN_FOCUS_TIMER) {
            // Pause/Resume Pomodoro timer
            timerRunning = !timerRunning;
            if (timerRunning) {
              if (timerRemainingSeconds == 0) {
                timerRemainingSeconds = focusMinutes * 60;
              }
              if (rtcFound) {
                timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
              }
            }
            Serial.printf("[POMO] Focus Timer -> Running: %d\n", timerRunning);
            displayNeedsUpdate = true;
          } else if (currentScreen == SCREEN_PC_METRICS) {
            netForceRefresh = true;
            Serial.println("[METRICS] Force refresh requested via Tactile Button tap!");
            soundShortChirp();
            displayNeedsUpdate = true;
          } else {
            // Feedback in menu
            ledState = !ledState;
            digitalWrite(ONBOARD_LED_PIN, ledState ? LOW : HIGH);
            displayNeedsUpdate = true;
          }
        }
      }
    }
  }

  // *** LONG PRESS ACTION (Hold for >= 600ms) ***
  if (tactileStableState == LOW && !tactileLongPressTriggered) {
    if (now - tactilePressStartTime >= 600) {
      tactileLongPressTriggered = true;
      Serial.println("[TAC] Long Press (Hold) detected!");

      if (alarmTriggered) {
        // "snooze after pressing and holding the tac button"
        alarmTriggered = false;
        snoozeActive = true;
        snoozeUntilEpoch = (rtcFound ? rtc.now().unixtime() : (millis() / 1000)) + 300; // 5 min snooze
        noTone(BUZZER_PIN);
        digitalWrite(ONBOARD_LED_PIN, HIGH);
        Serial.println("[ALARM] SNOOZED for 5 minutes via Tactile Button hold!");
        displayNeedsUpdate = true;
      } else if (currentScreen == SCREEN_ALARM) {
        // In Alarm screen: Hold tests the Harry Potter melody!
        alarmTriggered = true;
        alarmTriggerStartTime = now;
        Serial.println("[ALARM] Preview test: Playing Harry Potter Theme!");
        displayNeedsUpdate = true;
      } else if (currentScreen == SCREEN_FOCUS_TIMER) {
        // Reset timer
        timerRunning = false;
        timerRemainingSeconds = focusMinutes * 60;
        Serial.println("[POMO] Focus Timer Reset!");
        displayNeedsUpdate = true;
      } else if (currentScreen == SCREEN_PC_METRICS) {
        // Long press returns to menu
        currentScreen = SCREEN_MENU;
        soundShortChirp();
        displayNeedsUpdate = true;
      }
    }
  }
  tactileLastReading = reading;

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
          // Select exact number based on field toggled with Tac Button!
          if (currentAlarmField == EDIT_HOURS) {
            alarmHour = (alarmHour + delta + 24) % 24;
            Serial.printf("[ALARM] Set Hour: %02d\n", alarmHour);
          } else {
            alarmMinute = (alarmMinute + delta + 60) % 60;
            Serial.printf("[ALARM] Set Minute: %02d\n", alarmMinute);
          }
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
      snoozeActive = false;
      noTone(BUZZER_PIN);
      digitalWrite(ONBOARD_LED_PIN, HIGH);
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
