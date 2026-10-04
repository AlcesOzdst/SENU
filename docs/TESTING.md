# Testing Strategy

## 1. Firmware functionality

- Build firmware with `pio run`.
- On hardware, verify OLED shows startup banner and placeholder menu.
- Confirm firmware remains responsive (no boot loops) for extended idle run.

## 2. Desktop agent API

- Run `pytest` in `desktop_agent`.
- Verify:
  - `GET /health` returns success and service identifier.
  - `GET /metrics/system` returns CPU and memory fields within valid ranges.

## 3. Device ↔ desktop synchronization

Initial manual checks (after network integration begins):

- Device can reach desktop agent over local Wi-Fi.
- Device handles timeout/offline conditions without blocking core UI.
- Polling cadence does not interfere with local timer responsiveness.

## 4. Reliability checks

- Power-cycle test: 20 restart cycles with no corrupted startup state.
- Long-run idle test: 4+ hours without crashes or memory exhaustion.
- Intermittent network test: alternate online/offline desktop agent every minute and verify graceful UI behavior.

## 5. Comparative usability evaluation

SENU should not be declared productivity-improving before measurement.

Recommended measurements:

- Time-to-start focus session (device vs desktop app)
- Number of interaction steps for common actions
- Subjective interruption level after context switch

Collect repeated trials and compare against baseline desktop-only workflow.
