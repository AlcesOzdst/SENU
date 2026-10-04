#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {
constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;
constexpr int kOledReset = -1;
constexpr uint8_t kOledAddress = 0x3C;

Adafruit_SSD1306 display(kScreenWidth, kScreenHeight, &Wire, kOledReset);

const char* kMenuItems[] = {
    "Tasks (planned)",
    "Focus Timer",
    "PC Metrics",
    "Voice Notes TBD"
};
constexpr size_t kMenuItemCount = sizeof(kMenuItems) / sizeof(kMenuItems[0]);

void drawMenu(size_t selectedIndex) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("SENU");
  display.println("----");

  for (size_t i = 0; i < kMenuItemCount; ++i) {
    if (i == selectedIndex) {
      display.print("> ");
    } else {
      display.print("  ");
    }
    display.println(kMenuItems[i]);
  }
  display.display();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, kOledAddress)) {
    Serial.println("SSD1306 init failed");
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.println("SENU booting...");
  display.display();
  delay(1000);

  drawMenu(0);
}

void loop() {
  static size_t selected = 0;
  static unsigned long lastUpdate = 0;
  const unsigned long now = millis();

  if (now - lastUpdate >= 2000) {
    selected = (selected + 1) % kMenuItemCount;
    drawMenu(selected);
    lastUpdate = now;
  }

  delay(20);
}
