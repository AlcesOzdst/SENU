# SENU Engineering Logbook

This logbook serves as the chronological research and development journal for the **SENU** (System for Efficiency, Notes, and Utilities) project. Every technical decision, hardware experiment, software change, and measurement is recorded here with timestamps, rationales, and observed outcomes.

---

## Guiding Engineering Principles
1. **Evidence over assumptions:** Do not label planned features as implemented.
2. **Reliability over feature count:** A small, reliable device is more valuable than a feature-rich unstable device.
3. **Offline-first for core actions:** Clock and focus timer must not depend on Wi-Fi or PC availability.
4. **Measure before claiming efficiency:** Physical controls are only better when measured against the existing workflow.
5. **Delay irreversible work:** Validate pinouts, power, and signal integrity on breadboards before CAD/enclosure design.
6. **Document why, not only what:** Record trade-offs, blockers, and alternatives considered.

---

## Log Entries

### [2026-10-04 16:46:33 +05:30] — Project Onboarding & Architecture Synthesis
* **Author:** Antigravity (AI Assistant) & Parth
* **Milestone:** M0 — Planning & Feasibility Baseline
* **Objective:** Ingest Notion documentation export (`SENU — System for Efficiency, Notes, and Utilities 3ed2552ba98680b6a250dd1222aab37b.pdf`), review existing repository scaffold, and formulate architectural understanding.
* **Findings:**
  - The project explores whether dedicated physical controls (rotary encoder + OLED) reduce context switching and execution time for quick desk tasks (task checking, Pomodoro focus timer, PC CPU/RAM monitoring, voice notes).
  - ESP32-C3 SuperMini has only 13 user-accessible GPIOs (GPIO 0–10, 20, 21), with strapping pin sensitivities on GPIO 2, 8, and 9. Accommodating all target peripherals (I²C, encoder, 8 buttons, I²S audio, microphone) requires an intentional pin allocation strategy (or an I²C GPIO expander like PCF8574/MCP23017).
  - The bare 2-pin electret microphone capsule requires an analog preamp (MAX9814) and dedicated sampling pipeline before voice recording can be realized.
* **Decisions Made:**
  - Maintain a strict offline-first design for the OLED UI and Pomodoro timer.
  - Maintain separation of concerns: embedded firmware in `/firmware`, host companion in `/desktop_agent`.

---

### [2026-10-04 17:06:40 +05:30] — Git Workflow Restructure & Milestone M0 Kickoff
* **Author:** Antigravity & Parth
* **Milestone:** M0 — Hardware Feasibility & Bring-Up
* **Branch Strategy:**
  - `prod`: Production/stable branch (renamed from `main`).
  - `dev-HW`: Hardware feasibility, pin mapping, wiring schematics, and embedded test firmware.
  - `dev-SD`: Software companion service (FastAPI desktop agent, SQLite models, PC metrics).
* **Git Actions:**
  - Renamed local `main` to `prod` and pushed to remote `origin/prod`.
  - Created and checked out branch `dev-HW`.
  - Created branch `dev-SD`.
* **Hardware Feasibility (M0) Analysis:**
  - **Pin Budget Bottleneck Identified:** Connecting 8 buttons directly to GPIO would consume 8 out of 13 available pins. Adding I²C (2 pins: SDA, SCL) and Rotary Encoder (3 pins: CLK, DT, SW) totals 13 pins—leaving zero pins for MAX98357 I²S audio (3 pins), Status LED, or ADC.
  - **Recommendation:**
    1. For immediate breadboard prototype: allocate I²C on GPIO 4/5, encoder on GPIO 0/1/3, and start with 2 direct tactile buttons on GPIO 6/7.
    2. For full 8-button matrix: introduce an I²C GPIO expander (PCF8574 / MCP23017) on the existing I²C bus to gain 8–16 inputs with zero additional MCU pins.
  - **Power Rules:** 12V/5V external supply must feed 5V regulated power to the SuperMini 5V pin. 12V must NEVER touch any module logic or 3.3V rails.
* **Firmware Validation Task:**
  - Implemented standalone I²C Scanner & OLED Diagnostic Firmware in `firmware/src/main.cpp`.
  - Scans bus for `0x3C` (SSD1306) and `0x68` (DS3231).
  - Renders diagnostic patterns (splash screen, coordinate boundaries, live uptime counter, and onboard LED heartbeat).
