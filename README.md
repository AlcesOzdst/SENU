# SENU — System for Efficiency, Notes, and Utilities

SENU is a desk-based embedded productivity appliance built around an ESP32-C3. The project explores whether physical controls can make task browsing, focus sessions, and quick PC status checks more useful than software-only desktop workflows.

## Current scope

This repository currently provides a **minimal scaffold**:

- ESP32-C3 PlatformIO firmware skeleton with SSD1306 OLED startup/menu screen
- Python FastAPI desktop agent with health and system metrics endpoints
- Documentation for architecture, hardware planning, roadmap, and testing

## Planned features

- Task list browsing from physical controls
- Focus timer that works even without Wi-Fi
- Basic Windows PC metrics display (CPU/memory first)
- Future voice-note recording/playback (pending microphone front-end and storage path)

## Architecture (high level)

- **Firmware (`/firmware`)** handles local UI, controls, and eventually offline timer/clock behavior.
- **Desktop agent (`/desktop_agent`)** exposes local HTTP/JSON endpoints for PC metrics and synchronization.
- **Docs (`/docs`)** track decisions, constraints, and staged implementation.

See `/docs/ARCHITECTURE.md` for full component mapping.

## Repository structure

```text
SENU/
├── firmware/        # PlatformIO ESP32-C3 firmware scaffold
├── desktop_agent/   # FastAPI desktop companion service + tests
├── docs/            # Architecture, hardware, roadmap, testing docs
├── hardware/        # Placeholder location for design/schematic assets
├── LICENSE
└── README.md
```

## Hardware currently available

- ESP32-C3 SuperMini
- 128×64 SSD1306 I²C OLED
- Rotary encoder + 8 tactile buttons
- DS3231 RTC
- MAX98357 I²S amplifier + 0.5 W speaker
- Bare two-pin electret microphone capsule
- Status LED
- Breadboard/jumpers, 12 V/5 V supply

Potential additions (not assumed installed): MAX9814 preamp, microSD module, I²C GPIO expander.

## Quick start

### 1) Desktop agent

```bash
cd /home/runner/work/SENU/SENU/desktop_agent
python -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
uvicorn app.main:app --reload
```

Endpoints:

- `GET /health`
- `GET /metrics/system`

Run tests:

```bash
cd /home/runner/work/SENU/SENU/desktop_agent
pytest
```

### 2) Firmware (ESP32-C3)

```bash
cd /home/runner/work/SENU/SENU/firmware
pio run
```

Upload (when board connected):

```bash
pio run -t upload
```

## Current limitations

- GPIO mapping for encoder/buttons/RTC/audio is intentionally not finalized until wiring is verified.
- Firmware currently displays placeholder UI only (no input scanning, no timer logic yet).
- Microphone path is **not implemented**; bare electret requires preamp and validated sampling chain.
- No authentication or cloud services are included; local HTTP/JSON only.

## Recommended next step

Implement verified pin mapping and input scanning for rotary encoder + first two buttons, then bind one button to move through the placeholder menu while preserving build stability.
