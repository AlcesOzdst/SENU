#pragma once

// ==========================================
// SENU CONFIGURATION TEMPLATE: WI-FI & AGENT
// ==========================================
// Instructions:
// 1. Copy this file to 'config.h' in the same directory:
//    cp config.example.h config.h
// 2. Fill in your Wi-Fi SSID and password below.
// 3. 'config.h' is gitignored to keep your credentials private.

// Wi-Fi Access Point Credentials (2.4 GHz)
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
#endif

// Desktop Companion Agent (FastAPI Host LAN IP)
#ifndef AGENT_HOST
#define AGENT_HOST "192.168.1.100"
#endif

#ifndef AGENT_PORT
#define AGENT_PORT 8000
#endif

// Telemetry Polling Interval (in milliseconds)
#define METRICS_POLL_INTERVAL_MS 2500
