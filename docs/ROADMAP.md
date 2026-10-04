# SENU Roadmap

## Stage 0 — Scaffold and bring-up (current)

### Goals

- Establish repository layout and documentation baseline.
- Build firmware and desktop agent minimal skeletons.

### Acceptance criteria

- `pio run` succeeds for ESP32-C3 firmware scaffold.
- FastAPI app starts and serves health + system metrics.
- API tests pass locally.

## Stage 1 — Core interaction loop

### Goals

- Verify GPIO mapping for encoder and at least two buttons.
- Implement menu navigation using physical controls.

### Acceptance criteria

- User can move menu selection with encoder/button input.
- OLED updates are stable with no crashes over 1-hour idle test.

## Stage 2 — Offline timer/clock

### Goals

- Integrate DS3231 time source.
- Implement focus timer independent of Wi-Fi availability.

### Acceptance criteria

- Timer start/pause/reset works with physical input.
- Timer remains functional with desktop agent unavailable.

## Stage 3 — Desktop sync and metrics

### Goals

- Expand local HTTP APIs and polling strategy.
- Display selected PC metrics on device.

### Acceptance criteria

- Device displays CPU and memory data refreshed on schedule.
- Graceful fallback UI shown when agent is offline.

## Stage 4 — Evaluate productivity impact

### Goals

- Run comparative usability checks against desktop-only workflow.

### Acceptance criteria

- Documented measurements for task completion time and interruption cost.
- Evidence-based conclusion on whether dedicated hardware interaction helps.

## Stage 5 — Voice notes feasibility

### Goals

- Validate hardware path for input preamp + storage.
- Prototype constrained recording/playback flow.

### Acceptance criteria

- Confirmed, tested audio front-end and recording quality baseline.
- Decision log documenting whether feature is practical on current hardware.
