# ESP32-S3 Life Tracker — Daily Task Manager

**Board:** M5Stack StickS3 (SKU: K150)
**Server:** Render (Flask + SQLite)
**Status:** V2 — IMU features added
**Started:** August 2026

## Overview

A desk device that shows today's tasks, lets you check them off, and syncs to the cloud. Tasks are managed via a web UI on your phone — the StickS3 polls the server and displays them in real-time.

## Features

### V1 — Core Task Management
- Task list synced from cloud server (Render)
- Scroll through tasks with Button A
- Toggle done with Button B, scroll up with Button B hold
- Delete current task with Button A hold
- Apple Watch-style UI: dot indicators, "Today" header, progress bar
- Battery bar with charging indicator
- WiFi status indicator
- Web UI at `https://life-tracker-server-ipri.onrender.com/`

### V2 — IMU (BMI270)
- **Pick-up wake** — screen goes dark after 10s of stillness, wakes on motion
- **Orientation views** — vertical/flat = task list, rotated sideways = stats view
- **Shake to clear** — shake aggressively to delete all completed tasks (with A/B confirm)

## Controls

| Action | Input |
|---|---|
| Scroll down | Tap A |
| Scroll up | Hold B |
| Toggle task | Tap B |
| Delete task | Hold A |
| View stats | Rotate device sideways |
| Clear done tasks | Shake device → B to confirm |
| Screen wake | Pick up device |

## Architecture

```
Phone browser →  │ Flask Server (Render) │ → SQLite DB
                 └──────────┬───────────┘
                            │ WiFi
                            ▼
                    M5StickS3 (polls every 30s)
                       Displays task list (or stats)
                       POST done/undo/delete
```

## Repo Structure

```
├── firmware/life_tracker/    # Arduino sketch for the StickS3
├── server/
│   ├── task_api.py           # Flask API + web UI
│   ├── requirements.txt      # Python deps
│   └── Procfile              # Render config
├── docs/
│   ├── render-deploy.md      # Render deployment guide
│   ├── ui-styles.html        # UI style mockups
│   └── imu-ideas.html        # IMU feature brainstorm
└── README.md                 # This file
```

## Hardware — M5Stack StickS3

| Component | Spec |
|---|---|
| **SoC** | ESP32-S3-PICO-1-N8R8 (240MHz dual-core) |
| **Memory** | 8MB Flash + 8MB PSRAM |
| **Display** | 1.14" LCD, 135×240 (ST7789P3) |
| **WiFi** | 2.4 GHz 802.11 b/g/n |
| **Buttons** | Button A (front), Button B (right side) |
| **IMU** | BMI270 (6-axis accelerometer + gyroscope) |
| **Audio** | ES8311 codec + MEMS mic + speaker (disabled) |
| **Battery** | 250mAh LiPo |
| **Extras** | IR TX/RX, Grove port, Hat2 bus |

## Stack

- **Firmware:** Arduino (M5Unified, M5GFX, ArduinoJson, HTTPClient)
- **Backend:** Flask + SQLite + Gunicorn
- **Cloud:** Render (free tier)
- **Deployment:** Git push → auto-deploy

## Build Log

- **2026-08-30:** Project started, spec defined, repo created
- **2026-08-31:** V1 complete — scroll, toggle, delete, Render cloud sync, Apple Watch UI, speaker whine fix, battery bar
- **2026-09-01:** V2 — IMU: pick-up wake, orientation views, shake to clear

## Links

- [Product page](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit)
- [Arduino setup guide](https://docs.m5stack.com/en/arduino/m5sticks3/program)
- [Pinout / pinmap](https://docs.m5stack.com/en/core/StickS3#PinMap)