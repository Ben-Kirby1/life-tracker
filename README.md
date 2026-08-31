# ESP32-S3 Life Tracker — Daily Task Manager

**Status:** Active — V1 build in progress
**Board:** M5Stack StickS3 (SKU: K150)
**Started:** August 2026
**Goal:** Desk device that shows today's tasks, lets you check them off.

## Hardware — M5Stack StickS3 Specs

| Component | Spec |
|---|---|
| **SoC** | ESP32-S3-PICO-1-N8R8 (240MHz dual-core) |
| **Memory** | 8MB Flash + 8MB PSRAM |
| **Display** | 1.14" LCD, 135×240 (ST7789P3) |
| **WiFi** | 2.4 GHz 802.11 b/g/n |
| **Buttons** | Button A (front), Button B (right side) |
| **Battery** | 250mAh LiPo |
| **Extras** | IR TX/RX, MEMS mic + speaker, IMU (BMI270) |
| **Expansion** | Grove HY2.0-4P, Hat2 bus (2.54-16P) |

## V1 Feature — Daily Task List

**Core loop:**
1. Boot → Connect to WiFi → Fetch today's tasks from server
2. Display task list on screen (scrollable via buttons)
3. Check off tasks by pressing button
4. Mark "done" syncs back to server
5. Poll every 30s for updates

**What it shows on screen:**
- 📋 Task header: "Today's Tasks"
- ☐ Task 1 (title, ~30 chars max)
- ☐ Task 2
- ☐ Task 3
- (scroll if more than 5 fit)
- Bottom line: "2/6 done"

**Button mapping:**
- **Button A** (front) — Scroll down / next task
- **Button B** (right) — Toggle current task done/undone (long press to reset all)

## Architecture

```
Phone browser →  │ Flask Server (http://192.168.x.x:5000) │ → SQLite DB
                 └──────────────┬──────────────┘
                                │ WiFi
                                ▼
                        M5StickS3 (polls every 30s)
                           Displays task list
                           POST done/undo
```

## Repo Structure

```
├── firmware/          # Arduino code for the StickS3
│   └── life_tracker/  # Main sketch
├── server/            # Flask backend
│   └── app.py         # Task list API
├── docs/              # Build notes, wiring, screenshots
└── README.md          # This file
```

## V1 Build Order

| Step | What | Time |
|---|---|---|
| 1 | Install Arduino IDE + M5Stack boards + libraries | 30 min |
| 2 | Display "Hello World" on StickS3 screen | 15 min |
| 3 | Draw task list UI — scrollable text | 1 hour |
| 4 | Wire buttons — scroll and toggle | 1 hour |
| 5 | Write Flask server — POST task, GET list | 1 hour |
| 6 | WiFi connect + HTTP fetch tasks to display | 1 hour |
| 7 | Wire done toggle → POST to server | 1 hour |
| 8 | Polish: battery indicator, error handling, case | 2 hours |

**Total V1: ~7-8 hours of build time.**

## V2 Ideas (future)
- Voice input (MEMS mic → wake word → "add task buy milk")
- Pomodoro timer mode
- Habit streaks
- Weekly review — show completion rate
- IR control (turn off lights when you check off "bedtime routine")
- 3D-printed desk stand

## Links
- [Product page](https://shop.m5stack.com/products/m5sticks3-esp32s3-mini-iot-dev-kit)
- [Arduino setup guide](https://docs.m5stack.com/en/arduino/m5sticks3/program)
- [Pinout / pinmap](https://docs.m5stack.com/en/core/StickS3#PinMap)

## Build Log
- **2026-08-30:** Spec defined, repo created
- **V1 complete:** TBD