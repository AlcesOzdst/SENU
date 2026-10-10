#if __has_include("../include/config.h")
#include "../include/config.h"
#elif __has_include("config.h")
#include "config.h"
#elif __has_include("../include/config.example.h")
#include "../include/config.example.h"
#endif

// Safe fallback credentials if config.h is absent
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#endif

#ifndef AGENT_HOST
#define AGENT_HOST "192.168.1.100"
#endif

#ifndef AGENT_PORT
#define AGENT_PORT 8000
#endif

#ifndef METRICS_POLL_INTERVAL_MS
#define METRICS_POLL_INTERVAL_MS 2500
#endif
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_wifi.h>

#include "animotion.h"
#include "sample_animation.h"


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
constexpr int BUZZER_PIN =
    6; // Piezo/Active Buzzer Output (Pin 10 on right header)
constexpr int PAUSE_BTN_PIN =
    7; // External Tactile Push Button (Active LOW, Pin 11 on right header)

// Display Calibrated Geometry (New 128x64 OLED Panel)
constexpr int SCREEN_WIDTH = 128;
constexpr int SCREEN_HEIGHT = 64;
constexpr int SCREEN_Y_OFFSET = 0; // Full 128x64 active panel (Y = 0..63)
constexpr int USABLE_HEIGHT = 64;  // Full 64px display canvas

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RTC_DS3231 rtc;
bool rtcFound = false;
bool displayNeedsUpdate = true;

// Animotion Engine Player Instance
animotion::Player animPlayer;

void onAnimFrameRender(const uint8_t *bitmap, uint16_t width, uint16_t height,
                       uint16_t frame_index, void *user_data) {
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap, width, height, SSD1306_WHITE);
  display.display();
}

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

// ---------- Arc Slider Menu Items ----------
enum ArcMenuItem : uint8_t {
  ARC_MENU_STATS = 0,
  ARC_MENU_SHORT_BREAK = 1,
  ARC_MENU_FOCUS = 2,
  ARC_MENU_LONG_BREAK = 3,
  ARC_MENU_ALARM = 4,
  ARC_MENU_SETTINGS = 4,
  ARC_MENU_COUNT = 5
};

constexpr int TOTAL_MENU_ITEMS = ARC_MENU_COUNT;
int selectedMenuIndex = ARC_MENU_FOCUS; // Default highlight: Center FOCUS (2)

// ==========================================
// RETRO CAT POMODORO STATE & ASSETS
// ==========================================
enum PomoScreen : uint8_t {
  POMO_SCREEN_FOCUS,
  POMO_SCREEN_PAUSE,
  POMO_SCREEN_BREAK,
  POMO_SCREEN_COMPLETE
};
PomoScreen pomoScreen = POMO_SCREEN_FOCUS;

enum PomoSession : uint8_t {
  POMO_SESS_FOCUS,
  POMO_SESS_SHORT_BREAK,
  POMO_SESS_LONG_BREAK
};
PomoSession pomoSession = POMO_SESS_FOCUS;

uint16_t focusMinutes = 25;
uint16_t shortBreakMinutes = 5;
uint16_t longBreakMinutes = 15;
uint8_t completedSessions = 0;
uint8_t totalSessions = 0;
uint8_t pauseIndex = 0; // 0: RESUME, 1: +5 MIN, 2: RESET, 3: MENU

bool timerRunning = false;
bool sessionActive = false; // True when a focus or break session is active (running or paused)
uint32_t timerEndEpoch = 0;
uint32_t timerRemainingSeconds = 25 * 60;
uint32_t pomoCompleteStartTime = 0;

// Alarm State (Dialable to any minute, default 06:00 AM)
int alarmHour = 6;
int alarmMinute = 0;
bool alarmEnabled = true;
bool alarmTriggered = false;
unsigned long alarmTriggerStartTime = 0;

// Alarm Field Selection (Toggled via Tactile Button)
enum AlarmEditField { EDIT_HOURS, EDIT_MINUTES };
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

// Carousel Multi-Page State for Metrics
int metricsCarouselPage = 0; // 0: Live Gauges, 1: Network & Agent Telemetry
constexpr int TOTAL_METRICS_PAGES = 2;

// Sanitizes non-ASCII characters (e.g. Unicode curly apostrophe ’ -> ') for
// standard SSD1306 font
String sanitizeAscii(const String &in) {
  String out = "";
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if (c == 0xE2 && i + 2 < in.length() && (uint8_t)in[i + 1] == 0x80 &&
        (uint8_t)in[i + 2] == 0x99) {
      out += '\'';
      i += 2;
    } else if (c >= 32 && c <= 126) {
      out += (char)c;
    } else if (c > 127) {
      // Skip non-ASCII bytes
    } else {
      out += (char)c;
    }
  }
  return out;
}

// Carousel / Marquee text renderer for strings exceeding display margins
void drawCarouselText(const String &rawText, int x, int y, int maxChars,
                      unsigned long now) {
  String text = sanitizeAscii(rawText);
  display.setCursor(x, y);
  int len = text.length();
  if (len <= maxChars) {
    display.print(text);
    return;
  }
  // Smooth back-and-forth marquee ticker with stable hold times at edges
  int overflow = len - maxChars;
  int holdTicks = 4;
  int oneWayTicks = overflow + holdTicks;
  int cycleTicks = oneWayTicks * 2;
  int tickMs = 280; // 280ms per character step
  int phase = (now / tickMs) % cycleTicks;
  int offset = 0;
  if (phase < oneWayTicks) {
    offset = phase - (holdTicks / 2);
  } else {
    offset = (cycleTicks - 1 - phase) - (holdTicks / 2);
  }
  if (offset < 0)
    offset = 0;
  if (offset > overflow)
    offset = overflow;
  display.print(text.substring(offset, offset + maxChars));
}

void startWifiConnection(const char *ssid, const char *pass) {
  WiFi.disconnect();
  // Pass connect = false so we can set WPA3 SAE PWE parameters before handshake
  WiFi.begin(ssid, pass, 0, NULL, false);
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
    conf.sta.sae_pwe_h2e = WPA3_SAE_PWE_HUNT_AND_PECK;
    conf.sta.pmf_cfg.capable = true;
    conf.sta.pmf_cfg.required = true;
    esp_wifi_set_config(WIFI_IF_STA, &conf);
  }
  esp_wifi_connect();
}

void networkTask(void *pvParameters) {
  WiFi.persistent(false);
  WiFi.disconnect(true);
  vTaskDelay(pdMS_TO_TICKS(100));
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("[NET-EVENT] Disconnected. Reason code: %d\n",
                    info.wifi_sta_disconnected.reason);
    } else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
      Serial.println("[NET-EVENT] Associated with AP!");
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.printf("[NET-EVENT] Got IP: %s\n",
                    IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
    }
  });

  startWifiConnection(wifiSsid, wifiPass);
  netStatus = NET_CONNECTING;
  Serial.printf("[NET] Connecting to Wi-Fi SSID: '%s'...\n", wifiSsid);

  HTTPClient http;
  WiFiClient client;
  http.setTimeout(1500);

  unsigned long lastReconnectAttempt = millis();

  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      if (netStatus == NET_CONNECTING || netStatus == NET_IDLE) {
        netStatus = NET_CONNECTED;
        Serial.printf("[NET] Wi-Fi Connected! IP: %s | RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        displayNeedsUpdate = true;
      }

      char url[128];
      snprintf(url, sizeof(url), "http://%s:%d/metrics/system", agentHost,
               agentPort);
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
          Serial.printf("[NET] Agent offline or returned HTTP %d at %s\n",
                        httpCode, url);
        }
        netStatus = NET_AGENT_OFFLINE;
        displayNeedsUpdate = true;
      }
      http.end();
    } else {
      unsigned long nowTick = millis();
      if (nowTick - lastReconnectAttempt > 8000) {
        lastReconnectAttempt = nowTick;
        int wStatus = WiFi.status();
        Serial.printf("[NET] Reconnecting to Wi-Fi '%s'... (Status code: %d)\n",
                      wifiSsid, wStatus);
        if (wStatus == 1) { // 1 = WL_NO_SSID_AVAIL
          Serial.println(
              "[NET] Note: SSID not found. If using iPhone Hotspot, ensure "
              "'Maximize Compatibility' is ON (enables 2.4 GHz radio)!");
        }
        startWifiConnection(wifiSsid, wifiPass);
        netStatus = NET_CONNECTING;
        displayNeedsUpdate = true;
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
#define NOTE_AS4 466
#define NOTE_B4 494
#define NOTE_C5 523
#define NOTE_CS5 554
#define NOTE_D5 587
#define NOTE_DS5 622
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_FS5 740
#define NOTE_G5 784
#define NOTE_GS5 831
#define NOTE_A5 880
#define NOTE_AS5 932
#define NOTE_B5 988
#define NOTE_C6 1047
#define NOTE_CS6 1109
#define NOTE_D6 1175
#define NOTE_DS6 1245
#define NOTE_E6 1319
#define REST 0

struct NoteStep {
  uint16_t freq;
  uint16_t duration;
  uint16_t pause;
};

// Iconic Hedwig's Theme (Harry Potter main motif)
const NoteStep hedwigTheme[] = {
    // Opening motif
    {NOTE_B4, 350, 60},
    {NOTE_E5, 520, 60},
    {NOTE_G5, 200, 60},
    {NOTE_FS5, 350, 60},
    {NOTE_E5, 700, 60},
    {NOTE_B5, 350, 60},
    {NOTE_A5, 850, 100},
    {NOTE_FS5, 850, 100},

    // Second phrase
    {NOTE_E5, 520, 60},
    {NOTE_G5, 200, 60},
    {NOTE_FS5, 350, 60},
    {NOTE_DS5, 700, 60},
    {NOTE_F5, 350, 60},
    {NOTE_B4, 1000, 150},

    // Third phrase
    {NOTE_B4, 350, 60},
    {NOTE_E5, 520, 60},
    {NOTE_G5, 200, 60},
    {NOTE_FS5, 350, 60},
    {NOTE_E5, 700, 60},
    {NOTE_B5, 350, 60},
    {NOTE_D6, 700, 60},
    {NOTE_CS6, 350, 60},
    {NOTE_C6, 700, 60},
    {NOTE_GS5, 350, 60},

    // Fourth phrase (cadence)
    {NOTE_C6, 520, 60},
    {NOTE_B5, 200, 60},
    {NOTE_AS5, 350, 60},
    {NOTE_AS4, 700, 60},
    {NOTE_G5, 350, 60},
    {NOTE_E5, 1100, 400}};
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

void soundShortChirp() { tone(BUZZER_PIN, 1800, 20); }
void soundDoubleChirp() {
  tone(BUZZER_PIN, 1800, 25);
  delay(35);
  tone(BUZZER_PIN, 2400, 35);
}
void drawBoot(uint32_t now);

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
  Serial.printf(
      "[PINS] GPIO 7 level at boot: %d (1 = Open/HIGH, 0 = Pressed/LOW)\n",
      digitalRead(PAUSE_BTN_PIN));

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);

  bool displayReady = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!displayReady) {
    display.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }

  display.clearDisplay();
  display.setTextWrap(
      false); // Disable auto-wrap so lines never collide vertically
  display.display();

  // Initialize DS3231 RTC
  if (rtc.begin()) {
    rtcFound = true;
    Serial.println("[RTC] DS3231 detected successfully!");

    // Automatically sync RTC to your PC's exact time whenever uploaded!
    DateTime compileTime =
        DateTime(F(__DATE__), F(__TIME__)) + TimeSpan(0, 0, 0, 5);
    rtc.adjust(compileTime);
    Serial.printf("[RTC] Synced RTC to PC clock: %02d:%02d:%02d\n",
                  compileTime.hour(), compileTime.minute(),
                  compileTime.second());
  } else {
    Serial.println(
        "[RTC] Warning: DS3231 not found on I2C bus (offline fallback).");
  }

  // Launch Background Network Task for Wi-Fi & PC Metrics (FreeRTOS)
  xTaskCreate(networkTask, "senu_net", 6144, NULL, 1, &netTaskHandle);
  Serial.println("[NET] Background Wi-Fi & PC metrics polling task launched!");

  // Startup confirmation chirp
  soundShortChirp();

  // Play Animotion Boot Sequence
  animPlayer.begin(&ANIMATION_DATA, onAnimFrameRender);
  animPlayer.setLoop(false);
  Serial.println("[ANIMOTION] Boot animation playback started!");

  unsigned long bootStart = millis();
  while (!animPlayer.isFinished() && (millis() - bootStart < 2500)) {
    animPlayer.update(millis());
    delay(10);
    if (digitalRead(ENCODER_SW_PIN) == LOW || digitalRead(PAUSE_BTN_PIN) == LOW) {
      break;
    }
  }
}

// =====================================================
// ARC SLIDER MENU - FINAL UI (Reference Geometry)
// =====================================================
constexpr int ARC_CENTER_X = 64;
constexpr int ARC_CENTER_Y = 76;
constexpr int ARC_OUTER_R = 70;
constexpr int ARC_INNER_R = 30;
constexpr float PI_F = 3.14159265f;

void drawArcText(int x, int y, const char *str, uint16_t color = SSD1306_WHITE, uint8_t size = 1) {
  int16_t x1, y1;
  uint16_t tw, th;
  display.setTextSize(size);
  display.setTextColor(color);
  display.getTextBounds(str, 0, 0, &x1, &y1, &tw, &th);
  display.setCursor(x - (int)tw / 2, y);
  display.print(str);
}

void drawArc(int radius, uint16_t color = SSD1306_WHITE, bool breakCenter = false) {
  for (float deg = 180.0f; deg <= 360.0f; deg += 0.5f) {
    if (breakCenter && deg >= 252.0f && deg <= 288.0f) {
      continue; // Clean open archway under Sector 2 (FOCUS) for central digital clock
    }
    float rad = deg * PI_F / 180.0f;
    int x = ARC_CENTER_X + (int)roundf(radius * cosf(rad));
    int y = ARC_CENTER_Y + (int)roundf(radius * sinf(rad));
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
      display.drawPixel(x, y, color);
    }
  }
}

void drawArcSegmentLine(float angleDeg, uint16_t color = SSD1306_WHITE) {
  float rad = angleDeg * PI_F / 180.0f;
  float c = cosf(rad);
  float s = sinf(rad);
  int x1 = ARC_CENTER_X + (int)roundf(ARC_INNER_R * c);
  int y1 = ARC_CENTER_Y + (int)roundf(ARC_INNER_R * s);
  int x2 = ARC_CENTER_X + (int)roundf(ARC_OUTER_R * c);
  int y2 = ARC_CENTER_Y + (int)roundf(ARC_OUTER_R * s);
  display.drawLine(x1, y1, x2, y2, color);
}

void fillArcSector(uint8_t sector, uint16_t color = SSD1306_WHITE) {
  float startDeg = 180.0f + sector * 36.0f;
  float endDeg = startDeg + 36.0f;
  for (float a = startDeg; a <= endDeg; a += 0.3f) {
    float rad = a * PI_F / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    int x1 = ARC_CENTER_X + (int)roundf(ARC_INNER_R * c);
    int y1 = ARC_CENTER_Y + (int)roundf(ARC_INNER_R * s);
    int x2 = ARC_CENTER_X + (int)roundf(ARC_OUTER_R * c);
    int y2 = ARC_CENTER_Y + (int)roundf(ARC_OUTER_R * s);
    display.drawLine(x1, y1, x2, y2, color);
  }
}

void drawFocusIcon(int x, int y, uint16_t color) {
  // Laptop base line
  display.drawLine(x - 7, y + 4, x + 7, y + 4, color);
  // Laptop screen on right
  display.drawRect(x + 1, y - 5, 7, 8, color);
  display.drawPixel(x + 3, y - 2, color);
  // Cat head on left
  display.fillRoundRect(x - 8, y - 3, 8, 7, 2, color);
  // Cat ears
  display.drawPixel(x - 8, y - 5, color);
  display.drawPixel(x - 7, y - 4, color);
  display.drawPixel(x - 4, y - 5, color);
  display.drawPixel(x - 5, y - 4, color);
  // Cat paw on keyboard
  display.drawPixel(x, y + 3, color);
  // Eye
  uint16_t eyeCol = (color == SSD1306_BLACK) ? SSD1306_WHITE : SSD1306_BLACK;
  display.drawPixel(x - 6, y - 1, eyeCol);
}

void drawShortBreakIcon(int x, int y, uint16_t color) {
  display.fillRoundRect(x - 7, y - 2, 14, 8, 3, color);
  display.drawPixel(x + 4, y - 4, color);
  display.drawPixel(x + 5, y - 3, color);
  display.drawPixel(x + 1, y - 4, color);
  display.drawPixel(x + 2, y - 3, color);
  display.drawPixel(x - 8, y + 3, color);
  display.drawPixel(x - 8, y + 2, color);
  uint16_t eyeCol = (color == SSD1306_BLACK) ? SSD1306_WHITE : SSD1306_BLACK;
  display.drawLine(x + 2, y + 1, x + 5, y + 1, eyeCol);
  display.drawPixel(x + 8, y - 4, color);
  display.drawPixel(x + 10, y - 7, color);
}

void drawLongBreakIcon(int x, int y, uint16_t color) {
  uint16_t oppCol = (color == SSD1306_BLACK) ? SSD1306_WHITE : SSD1306_BLACK;
  display.fillCircle(x - 1, y, 6, color);
  display.fillCircle(x + 2, y - 3, 5, oppCol);
  display.drawPixel(x + 7, y - 3, color);
  display.drawPixel(x + 6, y - 3, color);
  display.drawPixel(x + 8, y - 3, color);
  display.drawPixel(x + 7, y - 4, color);
  display.drawPixel(x + 7, y - 2, color);
}

void drawStatsIcon(int x, int y, uint16_t color) {
  // Clean baseline with 3 ascending metric bars
  display.drawFastHLine(x - 6, y + 6, 13, color);
  display.fillRect(x - 5, y + 2, 2, 4, color);
  display.fillRect(x - 1, y - 2, 2, 8, color);
  display.fillRect(x + 3, y - 6, 2, 12, color);
}

void drawAlarmIcon(int x, int y, uint16_t color) {
  uint16_t oppCol = (color == SSD1306_BLACK) ? SSD1306_WHITE : SSD1306_BLACK;
  // Clock body
  display.fillCircle(x, y, 4, color);
  display.fillCircle(x, y, 3, oppCol);
  // Hands (10:10)
  display.drawLine(x, y, x - 2, y - 1, color);
  display.drawLine(x, y, x + 1, y - 2, color);
  display.drawPixel(x, y, color);
  // Twin bells on top
  display.drawPixel(x - 3, y - 5, color);
  display.drawPixel(x - 4, y - 4, color);
  display.drawPixel(x + 3, y - 5, color);
  display.drawPixel(x + 4, y - 4, color);
  // Hammer between bells
  display.drawPixel(x, y - 5, color);
  // Angled feet
  display.drawPixel(x - 3, y + 4, color);
  display.drawPixel(x - 4, y + 5, color);
  display.drawPixel(x + 3, y + 4, color);
  display.drawPixel(x + 4, y + 5, color);
}

void drawSettingsIcon(int x, int y, uint16_t color) {
  drawAlarmIcon(x, y, color);
}

void drawMenuCenterClock(int cx, int cy, uint32_t now) {
  int h = 0, m = 0;
  if (rtcFound) {
    DateTime cur = rtc.now();
    h = cur.hour();
    m = cur.minute();
  } else {
    unsigned long sec = millis() / 1000;
    h = (sec / 3600) % 24;
    m = (sec % 3600) / 60;
  }

  bool colon = (now % 1000) < 500;
  char timeBuf[8];
  snprintf(timeBuf, sizeof(timeBuf), "%02d%c%02d", h, colon ? ':' : ' ', m);

  // High-contrast, clean TextSize 1 digital clock (29px wide) centered at cx
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(cx - 14, cy);
  display.print(timeBuf);
}

void drawMenuStars() {
  display.drawPixel(4, 4, SSD1306_WHITE);
  display.drawPixel(123, 4, SSD1306_WHITE);
  display.drawPixel(4, 59, SSD1306_WHITE);
  display.drawPixel(123, 59, SSD1306_WHITE);

  // Left sky sparkles
  display.drawPixel(12, 12, SSD1306_WHITE);
  display.drawPixel(11, 12, SSD1306_WHITE);
  display.drawPixel(13, 12, SSD1306_WHITE);
  display.drawPixel(12, 11, SSD1306_WHITE);
  display.drawPixel(12, 13, SSD1306_WHITE);

  display.drawPixel(20, 10, SSD1306_WHITE);
  display.drawPixel(8, 24, SSD1306_WHITE);
  display.drawPixel(6, 36, SSD1306_WHITE);

  // Right sky sparkles
  display.drawPixel(115, 12, SSD1306_WHITE);
  display.drawPixel(114, 12, SSD1306_WHITE);
  display.drawPixel(116, 12, SSD1306_WHITE);
  display.drawPixel(115, 11, SSD1306_WHITE);
  display.drawPixel(115, 13, SSD1306_WHITE);

  display.drawPixel(107, 10, SSD1306_WHITE);
  display.drawPixel(119, 24, SSD1306_WHITE);
  display.drawPixel(121, 36, SSD1306_WHITE);
}

void drawMiniChar(int x, int y, char c, uint16_t color) {
  uint16_t glyph = 0;
  switch (c) {
    case 'S': case 's': glyph = 0b111100111001111; break;
    case 'T': case 't': glyph = 0b111010010010010; break;
    case 'A': case 'a': glyph = 0b010101111101101; break;
    default: glyph = 0; break;
  }
  for (int r = 0; r < 5; r++) {
    for (int col = 0; col < 3; col++) {
      int bitIdx = 14 - (r * 3 + col);
      if ((glyph >> bitIdx) & 1) {
        display.drawPixel(x + col, y + r, color);
      }
    }
  }
}

void drawMiniText(int x, int y, const char *str, uint16_t color) {
  int len = strlen(str);
  int totalW = len * 4 - 1;
  int startX = x - totalW / 2;
  for (int i = 0; i < len; i++) {
    drawMiniChar(startX + i * 4, y, str[i], color);
  }
}

void renderMenu() {
  uint32_t now = millis();

  // 1. Outer rounded boundary frame
  display.drawRoundRect(1, 1, 126, 62, 3, SSD1306_WHITE);

  // 2. Stars in the background sky
  drawMenuStars();

  // 3. Highlight the selected sector with solid white fill
  fillArcSector(selectedMenuIndex, SSD1306_WHITE);

  // 4. Draw outer and inner semicircular arcs (inner arc has line break under FOCUS)
  drawArc(ARC_OUTER_R, SSD1306_WHITE, false);
  drawArc(ARC_INNER_R, SSD1306_WHITE, true);

  // 5. Draw the 4 segment divider lines
  drawArcSegmentLine(216.0f, SSD1306_WHITE);
  drawArcSegmentLine(252.0f, SSD1306_WHITE);
  drawArcSegmentLine(288.0f, SSD1306_WHITE);
  drawArcSegmentLine(324.0f, SSD1306_WHITE);

  // 6. Draw each sector's icon & label (Clean wedge view: no duration text under names)
  // Sector 0: STAT (Compact 3x5 font fits cleanly inside Sector 0 column without overflow)
  {
    bool active = (selectedMenuIndex == ARC_MENU_STATS);
    uint16_t col = active ? SSD1306_BLACK : SSD1306_WHITE;
    drawMiniText(13, 47, "STAT", col);
  }

  // Sector 1: SHORT BREAK
  {
    bool active = (selectedMenuIndex == ARC_MENU_SHORT_BREAK);
    uint16_t col = active ? SSD1306_BLACK : SSD1306_WHITE;
    drawShortBreakIcon(35, 23, col);
    drawArcText(35, 35, "BREAK", col, 1);
  }

  // Sector 2: FOCUS (Positioned with line break spacing above digital clock)
  {
    bool active = (selectedMenuIndex == ARC_MENU_FOCUS);
    uint16_t col = active ? SSD1306_BLACK : SSD1306_WHITE;
    drawFocusIcon(64, 15, col);
    drawArcText(64, 27, "FOCUS", col, 1);
  }

  // Sector 3: LONG BREAK
  {
    bool active = (selectedMenuIndex == ARC_MENU_LONG_BREAK);
    uint16_t col = active ? SSD1306_BLACK : SSD1306_WHITE;
    drawLongBreakIcon(93, 23, col);
    drawArcText(93, 35, "LONG", col, 1);
  }

  // Sector 4: ALARM (Word "ALARM" replaced with alarm clock logo per request)
  {
    bool active = (selectedMenuIndex == ARC_MENU_ALARM);
    uint16_t col = active ? SSD1306_BLACK : SSD1306_WHITE;
    drawAlarmIcon(112, 44, col);
  }

  // 7. Central live current time (TextSize 1 with clean clearance)
  drawMenuCenterClock(ARC_CENTER_X, 52, now);

  // 8. Subtle status indicators in upper frame
  if (netStatus == NET_AGENT_ONLINE) {
    display.fillCircle(123, 6, 2, SSD1306_WHITE);
  } else if (WiFi.status() == WL_CONNECTED) {
    display.drawCircle(123, 6, 2, SSD1306_WHITE);
  }
}

void resetTimerToDefault() {
  focusMinutes = 25;
  shortBreakMinutes = 5;
  longBreakMinutes = 15;
  timerRunning = false;
  sessionActive = false;
  timerRemainingSeconds = 25 * 60;
  if (rtcFound) {
    timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
  }
  pomoScreen = POMO_SCREEN_FOCUS;
  pomoSession = POMO_SESS_FOCUS;
  pauseIndex = 0;
  soundDoubleChirp();
  Serial.println("[POMO] Reset timer to default 25m!");
  displayNeedsUpdate = true;
}

void executeMenuSingleTap(uint8_t item) {
  soundShortChirp();
  switch (item) {
  case ARC_MENU_FOCUS:
    if (sessionActive && timerRemainingSeconds > 0 && pomoSession == POMO_SESS_FOCUS) {
      // Restore saved Focus session
      currentScreen = SCREEN_FOCUS_TIMER;
      pomoScreen = timerRunning ? POMO_SCREEN_FOCUS : POMO_SCREEN_PAUSE;
      Serial.printf("[POMO] Restored saved FOCUS session (%lu s remaining)\n", (unsigned long)timerRemainingSeconds);
    } else {
      // Fresh session in customize mode (Break does NOT lock Focus!)
      pomoSession = POMO_SESS_FOCUS;
      pomoScreen = POMO_SCREEN_FOCUS;
      timerRemainingSeconds = (uint32_t)focusMinutes * 60;
      timerRunning = false;
      sessionActive = false;
      currentScreen = SCREEN_FOCUS_TIMER;
      Serial.printf("[MENU] Single-tap FOCUS -> Customize mode (%d min)\n", focusMinutes);
    }
    break;

  case ARC_MENU_SHORT_BREAK:
    if (sessionActive && timerRemainingSeconds > 0 && pomoSession == POMO_SESS_FOCUS) {
      // Focus is active! Block starting Short Break until Focus reaches 0 or is reset!
      currentScreen = SCREEN_FOCUS_TIMER;
      pomoScreen = timerRunning ? POMO_SCREEN_FOCUS : POMO_SCREEN_PAUSE;
      soundDoubleChirp();
      Serial.println("[POMO] FOCUS session active! Reset or let timer reach zero before starting BREAK.");
    } else if (sessionActive && timerRemainingSeconds > 0 && pomoSession == POMO_SESS_SHORT_BREAK) {
      // Re-enter currently active Short Break (running in background!)
      currentScreen = SCREEN_FOCUS_TIMER;
      pomoScreen = timerRunning ? POMO_SCREEN_BREAK : POMO_SCREEN_PAUSE;
      Serial.printf("[POMO] Restored running SHORT BREAK (%lu s remaining)\n", (unsigned long)timerRemainingSeconds);
    } else {
      pomoSession = POMO_SESS_SHORT_BREAK;
      pomoScreen = POMO_SCREEN_BREAK;
      timerRemainingSeconds = (uint32_t)shortBreakMinutes * 60;
      timerRunning = false;
      sessionActive = false;
      currentScreen = SCREEN_FOCUS_TIMER;
      Serial.printf("[MENU] Single-tap SHORT BREAK -> Customize mode (%d min)\n", shortBreakMinutes);
    }
    break;

  case ARC_MENU_LONG_BREAK:
    if (sessionActive && timerRemainingSeconds > 0 && pomoSession == POMO_SESS_FOCUS) {
      // Focus is active! Block starting Long Break until Focus reaches 0 or is reset!
      currentScreen = SCREEN_FOCUS_TIMER;
      pomoScreen = timerRunning ? POMO_SCREEN_FOCUS : POMO_SCREEN_PAUSE;
      soundDoubleChirp();
      Serial.println("[POMO] FOCUS session active! Reset or let timer reach zero before starting LONG BREAK.");
    } else if (sessionActive && timerRemainingSeconds > 0 && pomoSession == POMO_SESS_LONG_BREAK) {
      // Re-enter currently active Long Break (running in background!)
      currentScreen = SCREEN_FOCUS_TIMER;
      pomoScreen = timerRunning ? POMO_SCREEN_BREAK : POMO_SCREEN_PAUSE;
      Serial.printf("[POMO] Restored running LONG BREAK (%lu s remaining)\n", (unsigned long)timerRemainingSeconds);
    } else {
      pomoSession = POMO_SESS_LONG_BREAK;
      pomoScreen = POMO_SCREEN_BREAK;
      timerRemainingSeconds = (uint32_t)longBreakMinutes * 60;
      timerRunning = false;
      sessionActive = false;
      currentScreen = SCREEN_FOCUS_TIMER;
      Serial.printf("[MENU] Single-tap LONG BREAK -> Customize mode (%d min)\n", longBreakMinutes);
    }
    break;

  case ARC_MENU_STATS:
    currentScreen = SCREEN_PC_METRICS;
    Serial.println("[MENU] Single-tap -> Opened PC METRICS screen");
    break;

  case ARC_MENU_ALARM:
    currentScreen = SCREEN_ALARM;
    Serial.println("[MENU] Single-tap -> Opened ALARM screen");
    break;

  default:
    currentScreen = SCREEN_FOCUS_TIMER;
    break;
  }
  displayNeedsUpdate = true;
}

void activateArcMenuItem(uint8_t item) {
  executeMenuSingleTap(item);
}

// =====================================================
// RETRO CAT POMODORO GRAPHICS & SCENE ENGINE
// =====================================================

void textAt(int x, int y, const char *str, uint8_t size = 1, uint16_t color = SSD1306_WHITE) {
  display.setTextSize(size);
  display.setTextColor(color);
  display.setCursor(x, y);
  display.print(str);
}

void centeredText(int y, const char *str) {
  int16_t x1, y1;
  uint16_t tw, th;
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.getTextBounds(str, 0, y, &x1, &y1, &tw, &th);
  display.setCursor((SCREEN_WIDTH - tw) / 2, y);
  display.print(str);
}

void stars(uint8_t count, uint32_t now) {
  for (uint8_t i = 0; i < count; i++) {
    int x = 9 + ((i * 31 + 7) % 110);
    int y = 12 + ((i * 17 + 3) % 37);
    if (((now / 350) + i) % 3 == 0) {
      display.drawPixel(x, y, SSD1306_WHITE);
    } else {
      display.drawPixel(x, y, SSD1306_WHITE);
      display.drawPixel(x - 1, y, SSD1306_WHITE);
      display.drawPixel(x + 1, y, SSD1306_WHITE);
      display.drawPixel(x, y - 1, SSD1306_WHITE);
      display.drawPixel(x, y + 1, SSD1306_WHITE);
    }
  }
}

void drawBoot(uint32_t now) {
  display.drawRoundRect(1, 1, 126, 62, 3, SSD1306_WHITE);
  centeredText(7, "SENU POMODORO");
  stars(4, now);

  // Large cat peeking up.
  display.fillRoundRect(37, 28, 54, 30, 7, SSD1306_WHITE);
  display.fillTriangle(42, 35, 44, 21, 56, 33, SSD1306_WHITE);
  display.fillTriangle(71, 33, 83, 21, 86, 35, SSD1306_WHITE);

  // Face
  display.fillRoundRect(43, 31, 42, 25, 8, SSD1306_WHITE);
  display.fillCircle(54, 42, 3, SSD1306_BLACK);
  display.fillCircle(73, 42, 3, SSD1306_BLACK);
  display.drawPixel(63, 46, SSD1306_BLACK);
  display.drawLine(63, 47, 60, 50, SSD1306_BLACK);
  display.drawLine(63, 47, 66, 50, SSD1306_BLACK);

  // Paws on the desk
  display.fillRoundRect(24, 49, 25, 9, 3, SSD1306_WHITE);
  display.fillRoundRect(79, 49, 25, 9, 3, SSD1306_WHITE);
  display.drawFastHLine(8, 53, 112, SSD1306_WHITE);

  centeredText(55, "PRESS TO START");
}

void drawProgress(uint32_t leftMs, uint32_t totalMs) {
  display.drawRoundRect(5, 57, 118, 5, 2, SSD1306_WHITE);
  if (totalMs == 0) return;
  uint32_t elapsed = totalMs > leftMs ? totalMs - leftMs : 0;
  int width = (int)((uint64_t)elapsed * 112 / totalMs);
  if (width > 0)
    display.fillRect(8, 59, width, 1, SSD1306_WHITE);
}

void drawCountdown(uint32_t ms, int x, int y) {
  uint32_t secs = (ms + 999) / 1000;
  display.setTextColor(SSD1306_WHITE);
  if (secs >= 3600) {
    unsigned long h = secs / 3600;
    unsigned long m = (secs % 3600) / 60;
    unsigned long s = secs % 60;
    char hmBuf[8];
    snprintf(hmBuf, sizeof(hmBuf), "%lu:%02lu", h, m);
    display.setTextSize(2);
    display.setCursor(x, y);
    display.print(hmBuf);

    char sBuf[6];
    snprintf(sBuf, sizeof(sBuf), ":%02lu", s);
    display.setTextSize(1);
    display.setCursor(x + (int)strlen(hmBuf) * 12, y + 6);
    display.print(sBuf);
  } else {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu",
             (unsigned long)(secs / 60),
             (unsigned long)(secs % 60));
    display.setTextSize(2);
    display.setCursor(x, y);
    display.print(buf);
  }
}

// Compact, hand-drawn multi-pose pixel cat with rich living animations
void drawCat(int x, int y, uint8_t pose, uint32_t now) {
  int bob = (pose == 1 && (now / 280) % 2) ? 1 : 0;
  y += bob;

  // Pose 0: sitting; 1: studying; 2: sleeping; 3: celebrating.
  if (pose == 3) {
    // Raised paws
    display.drawLine(x + 8, y + 22, x + 1, y + 12, SSD1306_WHITE);
    display.drawLine(x + 1, y + 12, x + 4, y + 9, SSD1306_WHITE);
    display.drawLine(x + 31, y + 22, x + 39, y + 10, SSD1306_WHITE);
    display.drawLine(x + 39, y + 10, x + 35, y + 8, SSD1306_WHITE);
  }

  // Ears (left ear twitches adorably every ~2.5s when studying)
  bool earTwitch = (pose == 1) && (((now / 2400) % 4) == 0) && ((now % 2400) < 180);
  int leftEarTipY = earTwitch ? (y + 4) : (y + 1);
  display.fillTriangle(x + 5, y + 13, x + 7, leftEarTipY, x + 16, y + 10, SSD1306_WHITE);
  display.fillTriangle(x + 20, y + 10, x + 29, y + 1, x + 32, y + 13, SSD1306_WHITE);

  // Head
  display.fillRoundRect(x + 5, y + 9, 27, 22, 7, SSD1306_WHITE);

  // Ears cut out for pixel-art outline
  display.drawLine(x + 7, y + 9, x + 8, y + 5, SSD1306_BLACK);
  display.drawLine(x + 8, y + 5, x + 14, y + 10, SSD1306_BLACK);
  display.drawLine(x + 23, y + 10, x + 29, y + 5, SSD1306_BLACK);
  display.drawLine(x + 29, y + 5, x + 30, y + 9, SSD1306_BLACK);

  bool asleep = (pose == 2);
  bool blink = ((now / 3300) % 5 == 0) && ((now % 3300) < 150);

  // Eyes
  if (asleep || blink) {
    display.drawLine(x + 10, y + 19, x + 15, y + 19, SSD1306_BLACK);
    display.drawLine(x + 22, y + 19, x + 27, y + 19, SSD1306_BLACK);
  } else {
    display.fillCircle(x + 13, y + 19, 2, SSD1306_BLACK);
    display.fillCircle(x + 24, y + 19, 2, SSD1306_BLACK);
  }

  // Nose and smile
  display.drawPixel(x + 18, y + 22, SSD1306_BLACK);
  display.drawLine(x + 18, y + 23, x + 15, y + 25, SSD1306_BLACK);
  display.drawLine(x + 18, y + 23, x + 21, y + 25, SSD1306_BLACK);

  // Whiskers
  display.drawLine(x + 9, y + 23, x + 2, y + 22, SSD1306_BLACK);
  display.drawLine(x + 9, y + 26, x + 2, y + 27, SSD1306_BLACK);
  display.drawLine(x + 27, y + 23, x + 34, y + 22, SSD1306_BLACK);
  display.drawLine(x + 27, y + 26, x + 34, y + 27, SSD1306_BLACK);

  if (pose != 3) {
    // Body and paws
    display.fillRoundRect(x + 9, y + 29, 20, 11, 4, SSD1306_WHITE);
    display.drawLine(x + 12, y + 37, x + 12, y + 40, SSD1306_BLACK);
    display.drawLine(x + 25, y + 37, x + 25, y + 40, SSD1306_BLACK);

    // Animated 4-frame fluid tail swish
    uint8_t tailFrame = (now / 220) % 4;
    if (tailFrame == 0) {
      display.drawLine(x + 28, y + 35, x + 35, y + 34, SSD1306_WHITE);
      display.drawLine(x + 35, y + 34, x + 38, y + 30, SSD1306_WHITE);
    } else if (tailFrame == 1 || tailFrame == 3) {
      display.drawLine(x + 28, y + 35, x + 35, y + 31, SSD1306_WHITE);
      display.drawLine(x + 35, y + 31, x + 37, y + 25, SSD1306_WHITE);
    } else { // 2
      display.drawLine(x + 28, y + 35, x + 34, y + 28, SSD1306_WHITE);
      display.drawLine(x + 34, y + 28, x + 35, y + 21, SSD1306_WHITE);
    }

    // Animated typing front paw tapping the keyboard when studying
    if (pose == 1) {
      uint8_t typeCycle = (now / 180) % 2;
      display.fillCircle(x + 28 + (typeCycle ? 1 : 0), y + 35 - (typeCycle ? 1 : 0), 2, SSD1306_WHITE);
    }
  }

  // Floating music note / sparkle thought bubble drifting up from head during focus
  if (pose == 1) {
    uint32_t bubbleTime = (now + 600) % 3600;
    if (bubbleTime < 1800) {
      int bY = y - 2 - (int)(bubbleTime / 180);
      int bX = x + 31 + (int)((bubbleTime / 240) % 3);
      if (bY > 16) {
        if ((bubbleTime / 450) % 2 == 0) {
          // Floating ♪ note
          display.drawPixel(bX, bY, SSD1306_WHITE);
          display.drawLine(bX + 1, bY - 2, bX + 1, bY, SSD1306_WHITE);
          display.drawPixel(bX + 2, bY - 2, SSD1306_WHITE);
        } else {
          // Floating ✦ sparkle
          display.drawPixel(bX, bY, SSD1306_WHITE);
          display.drawPixel(bX - 1, bY, SSD1306_WHITE);
          display.drawPixel(bX + 1, bY, SSD1306_WHITE);
          display.drawPixel(bX, bY - 1, SSD1306_WHITE);
          display.drawPixel(bX, bY + 1, SSD1306_WHITE);
        }
      }
    }
  }

  if (asleep) {
    uint8_t zShift = (now / 350) % 4;
    textAt(x + 29 + zShift, y - 4 - zShift, "z");
    textAt(x + 36 + zShift, y - 9 - zShift, "Z");
  }
}

void drawPomoFocus(uint32_t now, uint32_t leftMs) {
  display.drawRoundRect(1, 1, 126, 62, 3, SSD1306_WHITE);

  textAt(6, 5, "FOCUS");
  char streakBuf[8];
  snprintf(streakBuf, sizeof(streakBuf), "%d/4", (completedSessions % 4) + 1);
  textAt(46, 5, streakBuf);

  // Session indicators (dots)
  for (uint8_t i = 0; i < 4; i++) {
    int x = 91 + i * 8;
    if (i < (completedSessions % 4))
      display.fillCircle(x, 9, 3, SSD1306_WHITE);
    else
      display.drawCircle(x, 9, 3, SSD1306_WHITE);
  }

  display.drawFastHLine(5, 16, 118, SSD1306_WHITE);

  drawCountdown(leftMs, 59, 19);

  // Desk scene
  display.drawLine(7, 54, 122, 54, SSD1306_WHITE);

  // Cozy lamp with subtle warm flicker
  display.drawLine(15, 29, 20, 42, SSD1306_WHITE);
  display.drawLine(20, 42, 14, 47, SSD1306_WHITE);
  display.fillTriangle(9, 31, 28, 31, 24, 39, SSD1306_WHITE);
  if ((now / 750) % 2 == 0) {
    display.drawPixel(22, 36, SSD1306_WHITE);
    display.drawPixel(25, 41, SSD1306_WHITE);
  }

  // Living animated cat studying
  drawCat(30, 29, 1, now);

  // Laptop with live animated code lines scrolling on screen
  display.drawLine(70, 44, 91, 44, SSD1306_WHITE);
  display.drawLine(74, 32, 91, 32, SSD1306_WHITE);
  display.drawLine(74, 32, 70, 44, SSD1306_WHITE);
  display.drawLine(91, 32, 91, 44, SSD1306_WHITE);
  display.drawLine(68, 46, 94, 46, SSD1306_WHITE);
  uint8_t codeFrame = (now / 350) % 4;
  if (codeFrame >= 1) display.drawFastHLine(76, 35, 6, SSD1306_WHITE);
  if (codeFrame >= 2) display.drawFastHLine(76, 38, 9, SSD1306_WHITE);
  if (codeFrame >= 3) display.drawFastHLine(76, 41, 5, SSD1306_WHITE);

  // Tiny plant with gently swaying leaf
  display.drawLine(109, 53, 109, 44, SSD1306_WHITE);
  display.drawLine(109, 48, 103, 43, SSD1306_WHITE);
  bool leafSway = (now / 1400) % 2;
  display.drawLine(109, 46, leafSway ? 116 : 115, leafSway ? 38 : 39, SSD1306_WHITE);
  display.drawLine(102, 53, 117, 53, SSD1306_WHITE);

  // Note: Bottom progress bar removed per request to maximize clean retro scene
}

void drawPomoPause(uint32_t now) {
  display.drawRoundRect(1, 1, 126, 62, 3, SSD1306_WHITE);

  centeredText(6, "PAUSED");

  // Show paused countdown timer on top-left above cat
  char timeBuf[12];
  uint32_t secs = timerRemainingSeconds;
  if (secs >= 3600) {
    snprintf(timeBuf, sizeof(timeBuf), "%lu:%02lu:%02lu",
             (unsigned long)(secs / 3600),
             (unsigned long)((secs % 3600) / 60),
             (unsigned long)(secs % 60));
  } else {
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu",
             (unsigned long)(secs / 60),
             (unsigned long)(secs % 60));
  }
  textAt(9, 16, timeBuf, 1, SSD1306_WHITE);

  // Sleeping/resting cat
  drawCat(9, 27, 2, now);

  const char *options[] = {"RESUME", "+5 MIN", "RESET", "MENU"};

  for (uint8_t i = 0; i < 4; i++) {
    int y = 16 + i * 11;
    if (i == pauseIndex) {
      display.fillRoundRect(62, y, 60, 10, 2, SSD1306_WHITE);
      textAt(64, y + 1, ">", 1, SSD1306_BLACK);
      textAt(72, y + 1, options[i], 1, SSD1306_BLACK);
    } else {
      textAt(72, y + 1, options[i], 1, SSD1306_WHITE);
    }
  }

  display.drawFastHLine(5, 60, 118, SSD1306_WHITE);
}

void drawPomoBreak(uint32_t now, uint32_t leftMs) {
  display.drawRoundRect(1, 1, 126, 62, 3, SSD1306_WHITE);

  textAt(6, 5, pomoSession == POMO_SESS_LONG_BREAK ? "LONG BRK" : "BREAK");

  for (uint8_t i = 0; i < 4; i++) {
    int x = 91 + i * 8;
    if (i < (completedSessions % 4))
      display.fillCircle(x, 9, 3, SSD1306_WHITE);
    else
      display.drawCircle(x, 9, 3, SSD1306_WHITE);
  }

  display.drawFastHLine(5, 16, 118, SSD1306_WHITE);

  drawCountdown(leftMs, 64, 19);

  // Window
  display.drawRect(7, 23, 25, 27, SSD1306_WHITE);
  display.drawFastVLine(19, 23, 27, SSD1306_WHITE);
  display.drawFastHLine(7, 36, 25, SSD1306_WHITE);
  display.drawPixel(12, 29, SSD1306_WHITE);
  display.drawPixel(27, 42, SSD1306_WHITE);

  // Moon
  display.fillCircle(25, 30, 4, SSD1306_WHITE);
  display.fillCircle(27, 28, 4, SSD1306_BLACK);

  // Napping cat on the desk
  display.drawFastHLine(5, 54, 118, SSD1306_WHITE);
  drawCat(34, 31, 2, now);

  // Small potted plant
  display.drawLine(109, 53, 109, 43, SSD1306_WHITE);
  display.drawLine(109, 48, 103, 43, SSD1306_WHITE);
  display.drawLine(109, 46, 115, 40, SSD1306_WHITE);
  display.drawLine(102, 53, 117, 53, SSD1306_WHITE);

  uint32_t totalMs = (uint32_t)(pomoSession == POMO_SESS_LONG_BREAK ? longBreakMinutes : shortBreakMinutes) * 60000UL;
  drawProgress(leftMs, totalMs);
}

void drawPomoComplete(uint32_t now) {
  display.drawRoundRect(1, 1, 126, 62, 3, SSD1306_WHITE);

  stars(10, now);

  centeredText(8, "WELL DONE!");

  // Happy jumping cat
  int bounce = ((now - pomoCompleteStartTime) / 120) % 2 ? 2 : 0;
  drawCat(45, 21 - bounce, 3, now);

  // Celebration sparkles
  for (int i = 0; i < 4; i++) {
    int x = 17 + i * 31;
    int y = 27 + ((i % 2) * 10);
    display.drawPixel(x, y, SSD1306_WHITE);
    display.drawPixel(x - 1, y, SSD1306_WHITE);
    display.drawPixel(x + 1, y, SSD1306_WHITE);
    display.drawPixel(x, y - 1, SSD1306_WHITE);
    display.drawPixel(x, y + 1, SSD1306_WHITE);
  }

  display.drawRoundRect(5, 57, 118, 5, 2, SSD1306_WHITE);
  display.fillRect(8, 59, 112, 1, SSD1306_WHITE);
}

void renderFocusTimer() {
  uint32_t now = millis();
  uint32_t leftMs = timerRemainingSeconds * 1000UL;

  switch (pomoScreen) {
  case POMO_SCREEN_FOCUS:
    drawPomoFocus(now, leftMs);
    break;
  case POMO_SCREEN_PAUSE:
    drawPomoPause(now);
    break;
  case POMO_SCREEN_BREAK:
    drawPomoBreak(now, leftMs);
    break;
  case POMO_SCREEN_COMPLETE:
    drawPomoComplete(now);
    break;
  }
}

void renderAlarmScreen() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  int curH = 0, curM = 0;
  if (rtcFound) {
    DateTime cur = rtc.now();
    curH = cur.hour();
    curM = cur.minute();
  } else {
    unsigned long sec = millis() / 1000;
    curH = (sec / 3600) % 24;
    curM = (sec % 3600) / 60;
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 3);
  if (alarmTriggered) {
    display.print("ALARM: HEDWIG THEME!");
  } else if (snoozeActive) {
    display.print("ALARM (SNOOZED +5M)");
  } else {
    display.print("SET ALARM");
    // Display current real-time in top-right header so user knows current time when setting alarm
    char nowBuf[16];
    snprintf(nowBuf, sizeof(nowBuf), "NOW %02d:%02d", curH, curM);
    display.setCursor(70, 3);
    display.print(nowBuf);
  }
  display.drawLine(2, 12, 125, 12, SSD1306_WHITE);

  unsigned long now = millis();

  if (alarmTriggered) {
    // Inverted Alert Header
    display.fillRect(4, 16, 120, 22, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(16, 20);
    display.print("WAKE UP!");

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    drawCarouselText("Hedwig's Theme Playing", 8, 42, 19, now);
    drawCarouselText("Tap:Off  Hold:Snooze", 4, 53, 20, now);
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
    char alarmStatusBuf[32];
    snprintf(alarmStatusBuf, sizeof(alarmStatusBuf), "Now:%02d:%02d Alarm:%s",
             curH, curM, alarmEnabled ? "ON (Hedwig)" : "OFF");
    drawCarouselText(alarmStatusBuf, 8, 39, 19, now);

    char alarmFootBuf[32];
    if (currentAlarmField == EDIT_HOURS) {
      snprintf(alarmFootBuf, sizeof(alarmFootBuf), "Tac:Min Knob:Hr Clk:%s",
               alarmEnabled ? "Off" : "On");
    } else {
      snprintf(alarmFootBuf, sizeof(alarmFootBuf), "Tac:Hr  Knob:Min Clk:%s",
               alarmEnabled ? "Off" : "On");
    }
    drawCarouselText(alarmFootBuf, 4, 53, 20, now);
  }
}

void renderPCMetrics() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Status Header: Title on Left, Carousel Slide Indicator on Right (No
  // overlap!)
  display.setCursor(4, 3);
  if (netStatus == NET_AGENT_ONLINE) {
    display.print("PC METRICS [OK]");
  } else if (netStatus == NET_AGENT_OFFLINE) {
    display.print("METRICS [OFFLINE]");
  } else if (netStatus == NET_CONNECTING) {
    display.print("METRICS [WIFI...]");
  } else {
    display.print("METRICS [NO WIFI]");
  }

  display.setCursor(104, 3);
  display.printf("%d/%d", metricsCarouselPage + 1, TOTAL_METRICS_PAGES);
  display.drawLine(2, 12, 125, 12, SSD1306_WHITE);

  unsigned long now = millis();

  if (metricsCarouselPage == 0) {
    // ==========================================
    // SLIDE 1: GAUGES & CPU / RAM UTILIZATION
    // ==========================================
    if (netStatus == NET_AGENT_ONLINE) {
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
      char memBuf[32];
      snprintf(memBuf, sizeof(memBuf), "RAM: %.1fG / %.1fG", usedGB, totGB);
      drawCarouselText(memBuf, 4, 41, 20, now);

      // Line 4: Footer (Y = 53)
      unsigned long secAgo = (now - lastMetricsFetchTime) / 1000;
      char liveBuf[32];
      snprintf(liveBuf, sizeof(liveBuf), "Upd: %lus ago [Tac:Ref]", secAgo);
      drawCarouselText(liveBuf, 4, 53, 20, now);
    } else {
      drawCarouselText("Agent: DISCONNECTED", 4, 17, 20, now);
      drawCarouselText("Run: desktop_agent/run.py", 4, 29, 20, now);
      drawCarouselText("Turn Knob -> Slide 2", 4, 41, 20, now);
      drawCarouselText("Tac:Poll  Clk:Menu", 4, 53, 20, now);
    }
  } else {
    // ==========================================
    // SLIDE 2: NETWORK & AGENT TELEMETRY
    // ==========================================
    char ssidBuf[48];
    snprintf(ssidBuf, sizeof(ssidBuf), "SSID: %s", wifiSsid);
    drawCarouselText(ssidBuf, 4, 17, 20, now);

    if (WiFi.status() == WL_CONNECTED) {
      char ipBuf[40];
      snprintf(ipBuf, sizeof(ipBuf), "IP: %s (%ddBm)",
               WiFi.localIP().toString().c_str(), WiFi.RSSI());
      drawCarouselText(ipBuf, 4, 29, 20, now);
    } else {
      drawCarouselText("WiFi: CONNECTING...", 4, 29, 20, now);
    }

    char hostBuf[48];
    snprintf(hostBuf, sizeof(hostBuf), "Host: %s:%d", agentHost, agentPort);
    drawCarouselText(hostBuf, 4, 41, 20, now);

    if (netStatus == NET_AGENT_ONLINE) {
      drawCarouselText("Status: AGENT ONLINE", 4, 53, 20, now);
    } else {
      drawCarouselText("Tac:Retry  Clk:Menu", 4, 53, 20, now);
    }
  }
}

void renderTasks() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 3);
  display.print("UPCOMING TASKS");
  display.setCursor(104, 3);
  display.print("4/4");
  display.drawLine(2, 12, 125, 12, SSD1306_WHITE);

  unsigned long now = millis();
  drawCarouselText("[ ] 1. Finish M3 Sync", 6, 16, 19, now);
  drawCarouselText("[ ] 2. Test Hedwig Song", 6, 28, 19, now);
  drawCarouselText("[x] 3. DS3231 RTC Clock", 6, 40, 19, now);
  drawCarouselText("[x] 4. New 128x64 OLED", 6, 52, 19, now);
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
  static bool pendingMenuSingleClick = false;
  static unsigned long pendingMenuClickTime = 0;
  static uint8_t pendingMenuItem = ARC_MENU_FOCUS;
  constexpr unsigned long DOUBLE_TAP_WINDOW_MS = 320;

  // 16-State Quadrature Transition Table
  static const int8_t qeTable[16] = {0,  -1, 1, 0, 1, 0, 0,  -1,
                                     -1, 0,  0, 1, 0, 1, -1, 0};
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
          Serial.printf(
              "[RTC] Clock successfully adjusted to %02d:%02d:%02d!\n", h, m,
              s);
          displayNeedsUpdate = true;
        }
      } else {
        Serial.println("[RTC] Format error! Use: SET HH:MM (e.g. SET 01:48)");
      }
    } else if (line.equalsIgnoreCase("SYNC")) {
      if (rtcFound) {
        DateTime compileTime =
            DateTime(F(__DATE__), F(__TIME__)) + TimeSpan(0, 0, 0, 6);
        rtc.adjust(compileTime);
        Serial.printf("[RTC] Synced to build time: %02d:%02d:%02d\n",
                      compileTime.hour(), compileTime.minute(),
                      compileTime.second());
        displayNeedsUpdate = true;
      }
    } else if (line.startsWith("WIFI ") || line.startsWith("wifi ")) {
      // Support WIFI "<SSID>" <PASSWORD> or WIFI <SSID> <PASSWORD>
      String rest = line.substring(5);
      rest.trim();
      String newSsid = "";
      String newPass = "";
      if (rest.startsWith("\"")) {
        int quoteEnd = rest.indexOf('"', 1);
        if (quoteEnd > 1) {
          newSsid = rest.substring(1, quoteEnd);
          newPass = rest.substring(quoteEnd + 1);
          newPass.trim();
        }
      } else {
        int lastSpace = rest.lastIndexOf(' ');
        if (lastSpace > 0) {
          newSsid = rest.substring(0, lastSpace);
          newPass = rest.substring(lastSpace + 1);
        }
      }
      newSsid.trim();
      newPass.trim();
      if (newSsid.length() > 0 && newPass.length() > 0) {
        strncpy(wifiSsid, newSsid.c_str(), sizeof(wifiSsid) - 1);
        strncpy(wifiPass, newPass.c_str(), sizeof(wifiPass) - 1);
        Serial.printf("[NET] Updated Wi-Fi credentials: SSID='%s'\n", wifiSsid);
        startWifiConnection(wifiSsid, wifiPass);
        netStatus = NET_CONNECTING;
        displayNeedsUpdate = true;
      } else {
        Serial.println("[NET] Format error! Use: WIFI <ssid> <password> or WIFI \"<ssid>\" <password>");
      }
    } else if (line.startsWith("HOST ") || line.startsWith("host ")) {
      // Format: HOST <IP>
      int firstSpace = line.indexOf(' ');
      String hostStr = line.substring(firstSpace + 1);
      hostStr.trim();
      strncpy(agentHost, hostStr.c_str(), sizeof(agentHost) - 1);
      Serial.printf("[NET] Updated Agent Host to: %s:%d\n", agentHost,
                    agentPort);
      netForceRefresh = true;
      displayNeedsUpdate = true;
    } else if (line.equalsIgnoreCase("STATUS")) {
      Serial.println("================ SENU STATUS ================");
      if (rtcFound) {
        DateTime cur = rtc.now();
        Serial.printf("RTC Time: %02d:%02d:%02d (%04d-%02d-%02d)\n", cur.hour(),
                      cur.minute(), cur.second(), cur.year(), cur.month(),
                      cur.day());
      }
      Serial.printf(
          "Wi-Fi Status: %s | SSID: %s\n",
          (WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED"),
          wifiSsid);
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("ESP32 IP: %s | RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
      }
      Serial.printf("Desktop Agent: http://%s:%d/metrics/system\n", agentHost,
                    agentPort);
      Serial.printf("Agent Status: %s\n",
                    (netStatus == NET_AGENT_ONLINE ? "ONLINE" : "OFFLINE"));
      Serial.printf("CPU: %.1f%% | RAM: %.1f%%\n", pcCpuPercent,
                    pcMemoryPercent);
      Serial.println("=============================================");
    } else if (line.equalsIgnoreCase("RESET") || line.equalsIgnoreCase("POMO RESET")) {
      resetTimerToDefault();
      Serial.println("[POMO] Pomodoro reset to default 25m via Serial command!");
      displayNeedsUpdate = true;
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
      if (currentTime.hour() == alarmHour &&
          currentTime.minute() == alarmMinute && currentTime.second() == 0) {
        alarmTriggered = true;
        snoozeActive = false;
        alarmTriggerStartTime = now;
        currentScreen = SCREEN_ALARM;
        displayNeedsUpdate = true;
        Serial.printf("[ALARM] >>> HARRY POTTER THEME ALARM TRIGGERED at "
                      "%02d:%02d! <<<\n",
                      alarmHour, alarmMinute);
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
        sessionActive = false;
        alarmTriggered = true;
        alarmTriggerStartTime = now;
        pomoCompleteStartTime = now;
        pomoScreen = POMO_SCREEN_COMPLETE;

        if (pomoSession == POMO_SESS_FOCUS) {
          completedSessions++;
          totalSessions++;
          // 4 completed focus sessions -> long break!
          pomoSession = (completedSessions % 4 == 0) ? POMO_SESS_LONG_BREAK : POMO_SESS_SHORT_BREAK;
        } else {
          pomoSession = POMO_SESS_FOCUS;
        }

        currentScreen = SCREEN_FOCUS_TIMER;
        displayNeedsUpdate = true;
        Serial.println("[POMO] Session COMPLETE! Celebrating with Retro Cat & Hedwig theme!");
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
          sessionActive = false;
          alarmTriggered = true;
          alarmTriggerStartTime = now;
          pomoCompleteStartTime = now;
          pomoScreen = POMO_SCREEN_COMPLETE;

          if (pomoSession == POMO_SESS_FOCUS) {
            completedSessions++;
            totalSessions++;
            pomoSession = (completedSessions % 4 == 0) ? POMO_SESS_LONG_BREAK : POMO_SESS_SHORT_BREAK;
          } else {
            pomoSession = POMO_SESS_FOCUS;
          }

          currentScreen = SCREEN_FOCUS_TIMER;
          displayNeedsUpdate = true;
          Serial.println("[POMO] Session COMPLETE! Celebrating with Retro Cat & Hedwig theme!");
        } else if (currentScreen == SCREEN_FOCUS_TIMER) {
          displayNeedsUpdate = true;
        }
      }
    }
  }

  // Auto-advance after 3 seconds on COMPLETE screen
  if (currentScreen == SCREEN_FOCUS_TIMER && pomoScreen == POMO_SCREEN_COMPLETE && (now - pomoCompleteStartTime >= 3000)) {
    alarmTriggered = false;
    noTone(BUZZER_PIN);
    pomoScreen = (pomoSession == POMO_SESS_FOCUS) ? POMO_SCREEN_FOCUS : POMO_SCREEN_BREAK;
    timerRemainingSeconds = (pomoSession == POMO_SESS_FOCUS) ? (focusMinutes * 60) :
                            ((pomoSession == POMO_SESS_LONG_BREAK ? longBreakMinutes : shortBreakMinutes) * 60);
    timerRunning = true;
    sessionActive = true;
    if (rtcFound) {
      timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
    }
    displayNeedsUpdate = true;
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
      Serial.println(
          "[ALARM] >>> SNOOZE EXPIRED! Playing Harry Potter Theme! <<<");
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
      Serial.printf("[TAC] GPIO 7 level -> %s\n", (tactileStableState == LOW)
                                                      ? "PRESSED (LOW)"
                                                      : "RELEASED (HIGH)");

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
            // "When I press the tacbutton it should toggle between hours and
            // minutes"
            currentAlarmField =
                (currentAlarmField == EDIT_HOURS) ? EDIT_MINUTES : EDIT_HOURS;
            Serial.printf("[ALARM] Toggle field -> Editing %s\n",
                          (currentAlarmField == EDIT_HOURS) ? "HOURS"
                                                            : "MINUTES");
            displayNeedsUpdate = true;
          } else if (currentScreen == SCREEN_FOCUS_TIMER) {
            if (alarmTriggered) {
              alarmTriggered = false;
              noTone(BUZZER_PIN);
              digitalWrite(ONBOARD_LED_PIN, HIGH);
              displayNeedsUpdate = true;
            } else if (pomoScreen == POMO_SCREEN_FOCUS || pomoScreen == POMO_SCREEN_BREAK) {
              if (timerRunning) {
                // Pause and switch to PAUSE screen
                timerRunning = false;
                pauseIndex = 0;
                pomoScreen = POMO_SCREEN_PAUSE;
                Serial.println("[POMO] Paused -> Switched to PAUSE screen");
              } else {
                // Start running
                timerRunning = true;
                sessionActive = true;
                if (timerRemainingSeconds == 0) {
                  timerRemainingSeconds = (pomoScreen == POMO_SCREEN_FOCUS ? focusMinutes :
                                          (pomoSession == POMO_SESS_LONG_BREAK ? longBreakMinutes : shortBreakMinutes)) * 60;
                }
                if (rtcFound) {
                  timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
                }
                Serial.println("[POMO] Timer started!");
              }
              displayNeedsUpdate = true;
            } else if (pomoScreen == POMO_SCREEN_PAUSE) {
              if (pauseIndex == 0) {
                // RESUME
                timerRunning = true;
                sessionActive = true;
                pomoScreen = (pomoSession == POMO_SESS_FOCUS) ? POMO_SCREEN_FOCUS : POMO_SCREEN_BREAK;
                if (timerRemainingSeconds == 0) {
                  timerRemainingSeconds = (pomoSession == POMO_SESS_FOCUS ? focusMinutes :
                                          (pomoSession == POMO_SESS_LONG_BREAK ? longBreakMinutes : shortBreakMinutes)) * 60;
                }
                if (rtcFound) {
                  timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
                }
                Serial.println("[POMO] Resumed!");
              } else if (pauseIndex == 1) {
                // ADD TIME (+5 MIN)
                timerRemainingSeconds += 300;
                if (rtcFound) {
                  timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
                }
                soundShortChirp();
                Serial.printf("[POMO] Added +5m via Tac Button! New remaining: %lu s\n", (unsigned long)timerRemainingSeconds);
              } else if (pauseIndex == 2) {
                // RESET TO DEFAULT 25
                resetTimerToDefault();
                Serial.println("[POMO] Reset to default 25m via Tac Button!");
              } else {
                // MENU: If Break session was active, keep it running in background!
                if (pomoSession != POMO_SESS_FOCUS && timerRemainingSeconds > 0) {
                  timerRunning = true;
                  sessionActive = true;
                  if (rtcFound) {
                    timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
                  }
                  Serial.printf("[POMO] Returned to SENU Menu - Break KEEPS RUNNING in background (%lu s left)!\n", (unsigned long)timerRemainingSeconds);
                } else {
                  timerRunning = false;
                  Serial.println("[POMO] Returned to SENU Menu (Focus saved)");
                }
                currentScreen = SCREEN_MENU;
              }
              displayNeedsUpdate = true;
            } else if (pomoScreen == POMO_SCREEN_COMPLETE) {
              alarmTriggered = false;
              noTone(BUZZER_PIN);
              pomoScreen = (pomoSession == POMO_SESS_FOCUS) ? POMO_SCREEN_FOCUS : POMO_SCREEN_BREAK;
              timerRemainingSeconds = (pomoSession == POMO_SESS_FOCUS ? focusMinutes :
                                      (pomoSession == POMO_SESS_LONG_BREAK ? longBreakMinutes : shortBreakMinutes)) * 60;
              timerRunning = true;
              if (rtcFound) {
                timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
              }
              displayNeedsUpdate = true;
            }
          } else if (currentScreen == SCREEN_PC_METRICS) {
            netForceRefresh = true;
            Serial.println(
                "[METRICS] Force refresh requested via Tactile Button tap!");
            soundShortChirp();
            displayNeedsUpdate = true;
          } else if (currentScreen == SCREEN_MENU) {
            executeMenuSingleTap(selectedMenuIndex);
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
        snoozeUntilEpoch =
            (rtcFound ? rtc.now().unixtime() : (millis() / 1000)) +
            300; // 5 min snooze
        noTone(BUZZER_PIN);
        digitalWrite(ONBOARD_LED_PIN, HIGH);
        Serial.println(
            "[ALARM] SNOOZED for 5 minutes via Tactile Button hold!");
        displayNeedsUpdate = true;
      } else if (currentScreen == SCREEN_ALARM) {
        // In Alarm screen: Hold tests the Harry Potter melody!
        alarmTriggered = true;
        alarmTriggerStartTime = now;
        Serial.println("[ALARM] Preview test: Playing Harry Potter Theme!");
        displayNeedsUpdate = true;
      } else if (currentScreen == SCREEN_FOCUS_TIMER) {
        if (timerRunning) {
          if (pomoSession == POMO_SESS_FOCUS) {
            // Pause focus timer & open pause screen without losing current time
            timerRunning = false;
            pomoScreen = POMO_SCREEN_PAUSE;
            pauseIndex = 0;
            Serial.println("[POMO] Focus paused via Tac Button Hold!");
          } else {
            // During Break: return directly to Menu while Break continues running in the background!
            currentScreen = SCREEN_MENU;
            Serial.println("[POMO] Returned to MENU via Tac Button Hold (Break keeps running)!");
          }
        } else {
          // In customize mode / idle: Hold resets timer back to default 25m!
          resetTimerToDefault();
          Serial.println("[POMO] Reset to default 25m via Tac Button Hold!");
        }
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
  uint8_t currentPins =
      (digitalRead(ENCODER_CLK_PIN) << 1) | digitalRead(ENCODER_DT_PIN);
  uint8_t lastPins = encoderHistory & 0x03;

  if (currentPins != lastPins) {
    encoderHistory = ((encoderHistory << 2) | currentPins) & 0x0F;
    int8_t step = qeTable[encoderHistory];

    if (step != 0) {
      accumulatedSteps += step;

      // Filter detent notches cleanly (accumulates 2 quadrature transitions per
      // click)
      if (accumulatedSteps >= 2 || accumulatedSteps <= -2) {
        int delta = (accumulatedSteps > 0) ? 1 : -1;
        accumulatedSteps = 0;

        if (currentScreen == SCREEN_MENU) {
          pendingMenuSingleClick = false; // Cancel pending click when scrolling
          selectedMenuIndex = (selectedMenuIndex + delta) % TOTAL_MENU_ITEMS;
          if (selectedMenuIndex < 0)
            selectedMenuIndex += TOTAL_MENU_ITEMS;
          Serial.printf("[ENC] Menu Scroll -> Index: %d\n", selectedMenuIndex);
          displayNeedsUpdate = true;
        } else if (currentScreen == SCREEN_FOCUS_TIMER) {
          if (pomoScreen == POMO_SCREEN_FOCUS && !timerRunning) {
            if (!sessionActive) {
              int newMin = (int)focusMinutes + delta;
              if (newMin < 1) newMin = 1;
              if (newMin > 240) newMin = 240; // Supports long hours up to 4 hours
              focusMinutes = (uint16_t)newMin;
              timerRemainingSeconds = (uint32_t)focusMinutes * 60;
              Serial.printf("[POMO] Focus set to: %d min\n", focusMinutes);
              displayNeedsUpdate = true;
            }
          } else if (pomoScreen == POMO_SCREEN_PAUSE) {
            pauseIndex = (pauseIndex + delta + 4) % 4;
            soundShortChirp();
            displayNeedsUpdate = true;
          } else if (pomoScreen == POMO_SCREEN_BREAK && !timerRunning) {
            if (!sessionActive) {
              if (pomoSession == POMO_SESS_LONG_BREAK) {
                int newMin = (int)longBreakMinutes + delta;
                if (newMin < 1) newMin = 1;
                if (newMin > 180) newMin = 180; // Supports long break up to 3 hours
                longBreakMinutes = (uint16_t)newMin;
                timerRemainingSeconds = (uint32_t)longBreakMinutes * 60;
                Serial.printf("[POMO] Long break set to: %d min\n", longBreakMinutes);
              } else {
                int newMin = (int)shortBreakMinutes + delta;
                if (newMin < 1) newMin = 1;
                if (newMin > 60) newMin = 60;
                shortBreakMinutes = (uint16_t)newMin;
                timerRemainingSeconds = (uint32_t)shortBreakMinutes * 60;
                Serial.printf("[POMO] Short break set to: %d min\n", shortBreakMinutes);
              }
              displayNeedsUpdate = true;
            }
          }
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
        } else if (currentScreen == SCREEN_PC_METRICS) {
          // Switch carousel slide in PC metrics
          metricsCarouselPage =
              (metricsCarouselPage + delta + TOTAL_METRICS_PAGES) %
              TOTAL_METRICS_PAGES;
          Serial.printf("[METRICS] Carousel Slide: %d\n",
                        metricsCarouselPage + 1);
          soundShortChirp();
          displayNeedsUpdate = true;
        }
      }
    }
  }

  // -------------------------------------------------------------
  // 4. Encoder Pushbutton (SW) with Short/Long Press
  // -------------------------------------------------------------
  int currentButtonState = digitalRead(ENCODER_SW_PIN);

  if (currentButtonState == LOW && lastButtonState == HIGH &&
      (now - lastButtonCheck > 50)) {
    buttonPressStart = now;
    buttonWasPressed = true;
    lastButtonCheck = now;
    displayNeedsUpdate = true;
  }

  // Long press (held for >= 600ms): Return to Main Menu immediately
  if (buttonWasPressed && currentButtonState == LOW &&
      (now - buttonPressStart >= 600)) {
    buttonWasPressed = false;
    pendingMenuSingleClick = false;
    ledState = !ledState;
    digitalWrite(ONBOARD_LED_PIN, ledState ? LOW : HIGH);
    currentScreen = SCREEN_MENU;
    if (pomoSession != POMO_SESS_FOCUS && sessionActive && timerRemainingSeconds > 0) {
      timerRunning = true;
      if (rtcFound) {
        timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
      }
      Serial.printf("[BTN] Long press -> Returned to MENU (Break keeps running in background, %lu s left)\n", (unsigned long)timerRemainingSeconds);
    } else {
      Serial.println("[BTN] Long press -> Returned to MENU");
    }
    displayNeedsUpdate = true;
  }

  // Short click (released before 600ms)
  if (buttonWasPressed && currentButtonState == HIGH &&
      lastButtonState == LOW) {
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
      if (pendingMenuSingleClick) {
        // DOUBLE-TAP detected within DOUBLE_TAP_WINDOW_MS:
        pendingMenuSingleClick = false;
        if (sessionActive && timerRemainingSeconds > 0 && pomoSession == POMO_SESS_FOCUS) {
          // Focus session already active! Restore Focus:
          currentScreen = SCREEN_FOCUS_TIMER;
          pomoScreen = timerRunning ? POMO_SCREEN_FOCUS : POMO_SCREEN_PAUSE;
          soundDoubleChirp();
          Serial.println("[MENU] Focus session already active -> Restored Focus.");
        } else {
          // Quick-start default 25m Pomodoro session immediately!
          // (Break does NOT lock Focus; double-tap starts Focus freely)
          pomoSession = POMO_SESS_FOCUS;
          pomoScreen = POMO_SCREEN_FOCUS;
          focusMinutes = 25;
          timerRemainingSeconds = 25 * 60;
          timerRunning = true;
          sessionActive = true;
          if (rtcFound) {
            timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
          }
          currentScreen = SCREEN_FOCUS_TIMER;
          soundDoubleChirp();
          Serial.println("[MENU] Double-tap detected -> Quick-started default 25m Pomodoro!");
        }
        displayNeedsUpdate = true;
      } else {
        // FIRST TAP: Start double-tap detection window
        pendingMenuSingleClick = true;
        pendingMenuClickTime = now;
        pendingMenuItem = (uint8_t)selectedMenuIndex;
      }
    } else if (currentScreen == SCREEN_FOCUS_TIMER) {
      if (alarmTriggered) {
        alarmTriggered = false;
        noTone(BUZZER_PIN);
        digitalWrite(ONBOARD_LED_PIN, HIGH);
        displayNeedsUpdate = true;
      } else if (pomoScreen == POMO_SCREEN_FOCUS || pomoScreen == POMO_SCREEN_BREAK) {
        if (timerRunning) {
          timerRunning = false;
          pauseIndex = 0;
          pomoScreen = POMO_SCREEN_PAUSE;
          Serial.println("[POMO] Paused via Knob click");
        } else {
          timerRunning = true;
          sessionActive = true;
          if (timerRemainingSeconds == 0) {
            timerRemainingSeconds = (pomoScreen == POMO_SCREEN_FOCUS ? focusMinutes :
                                    (pomoSession == POMO_SESS_LONG_BREAK ? longBreakMinutes : shortBreakMinutes)) * 60;
          }
          if (rtcFound) {
            timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
          }
          soundShortChirp();
          Serial.println("[POMO] Started via Knob click");
        }
        displayNeedsUpdate = true;
      } else if (pomoScreen == POMO_SCREEN_PAUSE) {
        if (pauseIndex == 0) {
          timerRunning = true;
          sessionActive = true;
          pomoScreen = (pomoSession == POMO_SESS_FOCUS) ? POMO_SCREEN_FOCUS : POMO_SCREEN_BREAK;
          if (timerRemainingSeconds == 0) {
            timerRemainingSeconds = (pomoSession == POMO_SESS_FOCUS ? focusMinutes :
                                    (pomoSession == POMO_SESS_LONG_BREAK ? longBreakMinutes : shortBreakMinutes)) * 60;
          }
          if (rtcFound) {
            timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
          }
          Serial.println("[POMO] Resumed via Knob click");
        } else if (pauseIndex == 1) {
          // ADD TIME (+5 MIN)
          timerRemainingSeconds += 300;
          if (rtcFound) {
            timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
          }
          soundShortChirp();
          Serial.printf("[POMO] Added +5m via Knob click! New remaining: %lu s\n", (unsigned long)timerRemainingSeconds);
        } else if (pauseIndex == 2) {
          // RESET TO DEFAULT 25
          resetTimerToDefault();
        } else {
          // MENU: If Break session was active, keep it running in background!
          if (pomoSession != POMO_SESS_FOCUS && timerRemainingSeconds > 0) {
            timerRunning = true;
            sessionActive = true;
            if (rtcFound) {
              timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
            }
            Serial.printf("[POMO] Returned to MENU via Pause screen - Break KEEPS RUNNING in background (%lu s left)!\n", (unsigned long)timerRemainingSeconds);
          } else {
            timerRunning = false;
            Serial.println("[POMO] Returned to MENU via Pause screen (Focus saved)");
          }
          currentScreen = SCREEN_MENU;
        }
        displayNeedsUpdate = true;
      } else if (pomoScreen == POMO_SCREEN_COMPLETE) {
        alarmTriggered = false;
        noTone(BUZZER_PIN);
        pomoScreen = (pomoSession == POMO_SESS_FOCUS) ? POMO_SCREEN_FOCUS : POMO_SCREEN_BREAK;
        timerRemainingSeconds = (pomoSession == POMO_SESS_FOCUS ? focusMinutes :
                                (pomoSession == POMO_SESS_LONG_BREAK ? longBreakMinutes : shortBreakMinutes)) * 60;
        timerRunning = true;
        if (rtcFound) {
          timerEndEpoch = rtc.now().unixtime() + timerRemainingSeconds;
        }
        displayNeedsUpdate = true;
      }
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

  // Check if Menu Single-Tap window expired without double-tap
  if (pendingMenuSingleClick && (now - pendingMenuClickTime >= DOUBLE_TAP_WINDOW_MS)) {
    pendingMenuSingleClick = false;
    if (currentScreen == SCREEN_MENU) {
      executeMenuSingleTap(pendingMenuItem);
      displayNeedsUpdate = true;
    }
  }

  // Refresh display at 80ms on Focus Timer & Menu for cat blinking animation, 140ms on
  // carousel marquee, 300ms on static screens
  unsigned long refreshInterval =
      (currentScreen == SCREEN_FOCUS_TIMER || currentScreen == SCREEN_MENU)
          ? 80
          : ((currentScreen == SCREEN_PC_METRICS ||
              currentScreen == SCREEN_TASKS || currentScreen == SCREEN_ALARM)
                 ? 140
                 : 300);
  if (now - lastClockTick >= refreshInterval) {
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
