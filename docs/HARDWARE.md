# SENU Hardware Specification & Pin Allocation (ESP32-C3 SuperMini)

## 1. ESP32-C3 SuperMini Pin Budget & Constraints

The ESP32-C3 SuperMini is an ultra-compact development board featuring a single-core 32-bit RISC-V microcontroller. It exposes **13 usable GPIO pins** arranged in two 8-pin rows.

### Header Pinout Reference

```text
               +-------------------+
               |     [ USB-C ]     |
      GPIO 0 --| 1               16 |-- 5V (VBUS / VIN)
      GPIO 1 --| 2               15 |-- GND
      GPIO 2 --| 3               14 |-- GPIO 21 (UART0 TX)
      GPIO 3 --| 4               13 |-- GPIO 20 (UART0 RX)
      GPIO 4 --| 5               12 |-- GPIO 10 (PWM/FSPI)
      GPIO 5 --| 6               11 |-- GPIO 9  (BOOT button / Strapping)
     3.3V OUT -| 7               10 |-- GPIO 8  (Blue LED / Strapping)
          GND -| 8                9 |-- GPIO 7  (FSPI / PWM)
               +-------------------+
                     [ ANTENNA ]
```

### Critical Strapping Pins & Behavioral Caveats
| Pin | Function & Sensitivity | Safe Usage Rule |
| :--- | :--- | :--- |
| **GPIO 2** | Strapping pin (must not be pulled low during reset) | Safe as input/output if pulled high/floating at boot. |
| **GPIO 8** | Strapping pin + **Onboard Blue LED** (Active LOW) | Must be HIGH during boot. Best reserved for status LED or left unburdened. |
| **GPIO 9** | Strapping pin + **Onboard BOOT Button** (Internal pull-up) | Pulling LOW during reset forces bootloader/download mode. Safe as general button after boot. |
| **GPIO 20 / 21** | Native UART0 (RX / TX) | Available as general I/O when using native USB-CDC over the USB-C connector. |

---

## 2. Complete Peripheral Interconnection Plan

| Peripheral Module | Signal / Pin Name | ESP32-C3 SuperMini Pin | Logic Voltage | Power Rail | Notes / Requirements |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **SSD1306 128×64 OLED** | **SDA** | **GPIO 4** | 3.3V | 3.3V | I²C Address `0x3C` (or `0x3D`). Shared bus. |
| | **SCL** | **GPIO 5** | 3.3V | 3.3V | 400 kHz Fast-mode I²C. |
| | VCC | 3.3V (or 5V if module has LDO) | — | 3.3V | Connect to SuperMini 3.3V rail. |
| | GND | GND | — | GND | Common ground. |
| **DS3231 RTC Module** | **SDA** | **GPIO 4** | 3.3V | 3.3V | Shared with OLED on I²C. Address `0x68`. |
| | **SCL** | **GPIO 5** | 3.3V | 3.3V | Shared with OLED on I²C. |
| | VCC | 3.3V | — | 3.3V | **CRITICAL:** Power with 3.3V when using standard CR2032 to avoid trickle charging! |
| | GND | GND | — | GND | Common ground. |
| **Rotary Encoder** | **CLK (A)** | **GPIO 0** | 3.3V | 3.3V | Quadrature encoder channel A with interrupt. |
| | **DT (B)** | **GPIO 1** | 3.3V | 3.3V | Quadrature encoder channel B. |
| | **SW (Push)**| **GPIO 3** | 3.3V | 3.3V | Momentary pushbutton (active LOW, `INPUT_PULLUP`). |
| | VCC | 3.3V | — | 3.3V | Power rail for encoder pull-up resistors. |
| | GND | GND | — | GND | Common ground. |
| **Tactile Buttons (M0/M1)**| **Btn 1 (Back/Exit)** | **GPIO 6** | 3.3V | — | Active LOW, internal `INPUT_PULLUP`. |
| | **Btn 2 (Select/Ok)** | **GPIO 7** | 3.3V | — | Active LOW, internal `INPUT_PULLUP`. |
| **MAX98357 I²S Amp** | **LRC (WS)** | **GPIO 20** | 3.3V | 5V | I²S Word Select (Left/Right Clock). |
| | **BCLK** | **GPIO 21** | 3.3V | 5V | I²S Bit Clock. |
| | **DIN** | **GPIO 10** | 3.3V | 5V | I²S Serial Audio Data. |
| | VIN | 5V Rail | — | 5V | Powering from 5V provides up to 3W into speaker. |
| | GND | GND | — | GND | Common ground. |
| | GAIN | GND | — | — | Floating or GND = 9 dB gain. |
| | SD_MODE | Floating (or 100k to 5V) | — | — | Mixes (L+R)/2 audio down to mono. |
| **0.5W Speaker** | + / - | MAX98357 OUT+ / OUT- | Analog | — | Connect directly to amplifier terminal block. |
| **Onboard Status LED** | Anode/Cathode | **GPIO 8** | 3.3V | Internal | Blue onboard LED, active LOW. Used as system heartbeat. |
| **MAX9814 Electret Preamp**| **OUT** | **GPIO 2 (ADC1_CH2)** | 0–3.3V | 3.3V | Analog audio input for voice notes (M4 milestone). |
| | VDD | 3.3V | — | 3.3V | Low-noise 3.3V supply. |
| | GND | GND | — | GND | Common ground. |

---

## 3. The 8-Button Feasibility Solution

Connecting 8 physical pushbuttons individually directly to the ESP32-C3 requires 8 separate GPIO lines.
Since the MCU only has 13 total pins, direct connection would leave only 5 pins, making it impossible to support I²C (2), Encoder (3), and I²S Audio (3).

### Architectural Strategy
1. **Milestone M0 & M1 (Current Bring-up):**
   - Use the **Rotary Encoder** (Rotate Up/Down + Push to Click) as primary navigation.
   - Use **2 direct tactile buttons** (GPIO 6 and GPIO 7) for "Back" and "Quick Action".
   - This provides complete UI navigation without any extra ICs.
2. **Milestone M2/M3 (Full 8 Buttons):**
   - Add a **PCF8574** or **MCP23017** I²C expander module.
   - The expander connects to the **same GPIO 4 (SDA) and GPIO 5 (SCL)** lines already used by the OLED and DS3231.
   - Provides 8 to 16 full digital inputs while using **zero additional GPIO pins** on the ESP32-C3.

---

## 4. Power Delivery & Electrical Safety Rules

> [!CAUTION]
> **OVERVOLTAGE HAZARD:** The bench supply provides 12V and 5V. **12V MUST NEVER BE CONNECTED TO THE ESP32-C3 OR ANY PERIPHERAL MODULE.** 12V will immediately destroy the silicon.

1. **5V Rail:** Feed regulated 5.0V from the bench supply or USB-C to the **5V pin** of the SuperMini and the **VIN pin** of the MAX98357 audio amplifier.
2. **3.3V Rail:** The ESP32-C3 SuperMini has an onboard linear regulator (LDO) outputting 3.3V on the **3.3V pin**. Use this 3.3V pin to power:
   - SSD1306 OLED display
   - DS3231 RTC
   - Rotary Encoder pull-ups
   - MAX9814 microphone preamp
3. **Common Ground:** All GND pins from the power supply, ESP32-C3, OLED, RTC, buttons, and amplifier MUST be tied together on the breadboard ground rail.

---

## 5. Milestone M0 Bring-Up Wiring (OLED Test)

For testing the OLED display right now, wire only the following 4 connections:

```text
ESP32-C3 SuperMini          SSD1306 128x64 OLED
------------------          -------------------
3.3V pin           ------>  VCC (or 5V if module has 662K regulator)
GND pin            ------>  GND
GPIO 4             ------>  SDA
GPIO 5             ------>  SCL
```
