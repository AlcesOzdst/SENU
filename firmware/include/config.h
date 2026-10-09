#pragma once

// ==========================================
// SENU CONFIGURATION: WI-FI & DESKTOP AGENT
// ==========================================

// Wi-Fi Access Point Credentials (2.4 GHz)
// Note: You can also adjust these dynamically at runtime via the Serial Monitor!
// Send: WIFI <ssid> <password>
#ifndef WIFI_SSID
#define WIFI_SSID "Parth"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "parth123"
#endif

// Desktop Companion Agent (FastAPI Host)
// Send: HOST <ip> via Serial Monitor to adjust dynamically.
#ifndef AGENT_HOST
#define AGENT_HOST "172.20.10.3"
#endif

#ifndef AGENT_PORT
#define AGENT_PORT 8000
#endif

// Telemetry Polling Interval (in milliseconds)
#define METRICS_POLL_INTERVAL_MS 2500
