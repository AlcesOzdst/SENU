# Hardware Notes and Constraints

## Known inventory

- ESP32-C3 SuperMini
- 128×64 SSD1306 I²C OLED
- Rotary encoder + 8 tactile buttons
- DS3231 RTC (I²C)
- MAX98357 I²S amplifier + 0.5 W speaker
- Bare two-pin electret microphone capsule
- Status LED
- Breadboard, jumpers, 12 V/5 V power supply

Optional (not assumed connected):

- MAX9814 microphone preamplifier
- microSD SPI module/card
- I²C GPIO expander

## Tentative interconnects (**unverified wiring**)

> The following assignments are placeholders only. Validate against your exact ESP32-C3 SuperMini pinout before soldering or coding fixed mappings.

- I²C bus shared by SSD1306 + DS3231
- Encoder channels + push switch on available digital GPIO
- 8 buttons either direct GPIO or via future expander
- MAX98357 over I²S pins
- Status LED on one output GPIO through suitable resistor

## GPIO constraints

- ESP32-C3 has limited GPIO; count required pins before assigning all inputs directly.
- Prefer preserving pins needed for serial/programming and boot behavior.
- If pin pressure is high, prioritize:
  1. I²C display/RTC
  2. Encoder + core buttons
  3. Audio output
  4. Optional expansion (GPIO expander / microSD)

## Power and safety checks

- **Do not connect 12 V directly** to ESP32, OLED, RTC, amplifier logic pins, or microphone modules.
- Ensure shared ground between ESP32 and peripheral modules.
- Verify amplifier and speaker wiring against MAX98357 reference.
- Confirm 5 V/3.3 V levels per module datasheets before powering.

## Voice-input caveat

A bare electret capsule cannot be treated as directly usable for robust recording on ESP32-C3 in this build. Required path:

1. Add validated preamplifier/front-end (e.g., MAX9814 or equivalent).
2. Confirm ADC-compatible signal levels and noise behavior.
3. Provide storage target (e.g., microSD) and file format strategy.
4. Validate end-to-end recording/playback quality before feature claims.

## Unresolved decisions

- Final GPIO map for encoder + 8 buttons
- Whether to adopt I²C GPIO expander immediately or defer
- Whether audio capture will use analog ADC path or a digital microphone option
- Storage selection/timing for voice notes (if implemented)
