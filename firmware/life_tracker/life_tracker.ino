#include <M5Unified.h>
#include <utility/power/M5PM1_Class.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ══════════════════════════════════════════════════════════════════════════
//  CONFIGURATION
// ══════════════════════════════════════════════════════════════════════════

const char* WIFI_SSID = "University House Midtown";
const char* WIFI_PASS = "Cat-Lime~Ragdoll";
const char* SERVER_HOST = "life-tracker-server-ipri.onrender.com";

// ══════════════════════════════════════════════════════════════════════════
//  TASK DATA
// ══════════════════════════════════════════════════════════════════════════

#define MAX_TASKS 10
#define MAX_TITLE 35

struct Task {
  int id;
  char title[MAX_TITLE];
  bool done;
};

Task taskList[MAX_TASKS];
int taskCount = 0;
int currentIndex = 0;
int scrollOffset = 0;
bool wifiConnected = false;

enum PendingAction { NONE, PENDING_TOGGLE, PENDING_DELETE };
PendingAction pendingAction = NONE;
int pendingId = 0;

// ══════════════════════════════════════════════════════════════════════════
//  SCREEN LAYOUT
// ══════════════════════════════════════════════════════════════════════════

#define HEADER_Y       18
#define LIST_START_Y   40
#define LIST_ROW_H     17
#define PROGRESS_BAR_Y 96
#define VISIBLE_TASKS  3

// ─── Colors ──────────────────────────────────────────────────────────────
#define BG_COLOR       TFT_BLACK
#define GREEN          0x07E0
#define GRAY           0x8410
#define DIM            0x4208
#define DIVIDER_COLOR  0x2104

// ══════════════════════════════════════════════════════════════════════════
//  IMU STATE
// ══════════════════════════════════════════════════════════════════════════

// Screen modes
enum ScreenMode { MODE_TASKS, MODE_STATS };
ScreenMode screenMode = MODE_TASKS;

// Pick-up wake
bool screenAwake = true;
unsigned long lastMotionTime = 0;
const unsigned long SLEEP_TIMEOUT = 10000;

// Shake to clear
bool shakeConfirmPending = false;
unsigned long shakeConfirmStart = 0;
const unsigned long SHAKE_CONFIRM_TIMEOUT = 3000;

// Gyro-based rotation detection
float rotationAngle = 0.0;          // cumulative rotation around Z axis
unsigned long lastGyroTime = 0;
const float ROTATION_THRESHOLD = 90.0;  // degrees to trigger mode switch
const float GYRO_DEADBAND = 10.0;       // deg/s — ignore tiny movements

// Stillness detection
const float STILL_THRESHOLD = 0.15;  // G deviation from 1.0G
const float SHAKE_THRESHOLD = 2.8;   // G peak for shake

// ══════════════════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════════════════

void setup() {
  M5.begin();
  M5.Lcd.setRotation(1);

  // Kill speaker whine
  M5.Speaker.end();
  m5::M5PM1_Class pmu;
  if (pmu.begin()) {
    pmu.setGPIOFunction(m5::M5PM1_Class::gpio3, m5::M5PM1_Class::gpio);
    pmu.setGPIOMode(m5::M5PM1_Class::gpio3, m5::M5PM1_Class::output);
    pmu.setGPIODrive(m5::M5PM1_Class::gpio3, m5::M5PM1_Class::push_pull);
    pmu.setGPIOOutput(m5::M5PM1_Class::gpio3, false);
  }
  M5.Power.setExtOutput(false);
  M5.Lcd.setBrightness(10);

  // Init IMU
  M5.Imu.begin();

  // Connect WiFi
  showSplash();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    fetchTasks();
  }

  drawScreen();
}

void showSplash() {
  M5.Lcd.fillScreen(BG_COLOR);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(GRAY);
  M5.Lcd.setCursor(10, 50);
  M5.Lcd.print("Connecting...");
  M5.Lcd.setCursor(10, 62);
  M5.Lcd.printf("WiFi: %s", WIFI_SSID);
}

// ══════════════════════════════════════════════════════════════════════════
//  MAIN LOOP
// ══════════════════════════════════════════════════════════════════════════

void loop() {
  M5.update();

  // ── Read IMU ──────────────────────────────────────────────────────────
  M5.Imu.update();
  float ax, ay, az;
  float gx, gy, gz;
  M5.Imu.getAccel(&ax, &ay, &az);
  M5.Imu.getGyro(&gx, &gy, &gz);
  float mag = sqrt(ax*ax + ay*ay + az*az);

  // ── Pick-up Wake ──────────────────────────────────────────────────────
  bool isStill = (mag > 1.0 - STILL_THRESHOLD && mag < 1.0 + STILL_THRESHOLD);

  if (isStill) {
    // Device is sitting still — track how long
    if (screenAwake && millis() - lastMotionTime > SLEEP_TIMEOUT) {
      screenAwake = false;
      M5.Lcd.fillScreen(BG_COLOR);  // turn off display
      M5.Lcd.setBrightness(0);
    }
  } else {
    // Device is moving
    lastMotionTime = millis();
    if (!screenAwake) {
      screenAwake = true;
      M5.Lcd.setBrightness(10);
      drawScreen();
    }
  }

  // Don't process anything else if screen is asleep
  if (!screenAwake) {
    delay(100);
    return;
  }

  // ── Orientation — Gyro-Based Rotation ────────────────────────────────
  // Integrate gyro Z-axis to track cumulative rotation angle
  // gz is in degrees per second, multiply by dt for degrees
  if (fabs(gz) > GYRO_DEADBAND) {
    unsigned long now = millis();
    if (lastGyroTime > 0) {
      float dt = (now - lastGyroTime) / 1000.0;  // seconds
      rotationAngle += gz * dt;
    }
    lastGyroTime = now;

    // If we've rotated past the threshold, toggle mode
    if (fabs(rotationAngle) > ROTATION_THRESHOLD) {
      screenMode = (screenMode == MODE_TASKS) ? MODE_STATS : MODE_TASKS;
      rotationAngle = 0.0;
      // Set the display rotation to match
      M5.Lcd.setRotation(screenMode == MODE_STATS ? 3 : 1);
      drawScreen();
    }
  } else {
    // Slowly decay rotation angle when still (prevents drift)
    rotationAngle *= 0.95;
    lastGyroTime = 0;
  }

  // ── Shake Detection ───────────────────────────────────────────────────
  static unsigned long lastShakePeak = 0;
  static int shakePeakCount = 0;
  static unsigned long shakeWindowStart = 0;

  if (mag > SHAKE_THRESHOLD) {
    unsigned long now = millis();
    if (now - shakeWindowStart > 1000) {
      // Reset window
      shakePeakCount = 0;
      shakeWindowStart = now;
    }
    if (now - lastShakePeak > 100) {  // debounce peaks
      shakePeakCount++;
      lastShakePeak = now;
    }
  }

  // If 3 peaks in 1 second = shake detected
  if (shakePeakCount >= 3 && !shakeConfirmPending && taskCount > 0) {
    shakeConfirmPending = true;
    shakeConfirmStart = millis();
    drawScreen();
    shakePeakCount = 0;
  }

  // Handle shake confirmation timeout
  if (shakeConfirmPending && millis() - shakeConfirmStart > SHAKE_CONFIRM_TIMEOUT) {
    shakeConfirmPending = false;
    drawScreen();
  }

  // ── Buttons ───────────────────────────────────────────────────────────
  // Shake confirm: tap B to confirm, tap A to cancel
  if (shakeConfirmPending) {
    if (M5.BtnB.wasPressed()) {
      // Confirm: delete all done tasks
      deleteAllDone();
      shakeConfirmPending = false;
      drawScreen();
    } else if (M5.BtnA.wasPressed()) {
      shakeConfirmPending = false;
      drawScreen();
    }
    delay(50);
    return;
  }

  // Button A: short = scroll down, long = delete current task
  if (M5.BtnA.wasPressed()) {
    if (screenMode == MODE_TASKS) {
      if (currentIndex < taskCount - 1) {
        currentIndex++;
        if (currentIndex - scrollOffset >= VISIBLE_TASKS) {
          scrollOffset = currentIndex - VISIBLE_TASKS + 1;
        }
      } else {
        currentIndex = 0;
        scrollOffset = 0;
      }
      drawScreen();
    }
  }

  if (M5.BtnA.wasHold()) {
    if (screenMode == MODE_TASKS && taskCount > 0 && currentIndex < taskCount) {
      int id = taskList[currentIndex].id;
      memmove(&taskList[currentIndex], &taskList[currentIndex + 1],
              (taskCount - currentIndex - 1) * sizeof(Task));
      taskCount--;
      if (currentIndex >= taskCount) currentIndex = max(0, taskCount - 1);
      drawScreen();
      pendingAction = PENDING_DELETE;
      pendingId = id;
    }
  }

  // Button B: tap = toggle, hold = scroll up
  if (M5.BtnB.wasPressed()) {
    if (screenMode == MODE_TASKS && taskCount > 0 && currentIndex < taskCount) {
      taskList[currentIndex].done = !taskList[currentIndex].done;
      drawScreen();
      pendingAction = PENDING_TOGGLE;
      pendingId = taskList[currentIndex].id;
    }
  }

  if (M5.BtnB.wasHold()) {
    if (screenMode == MODE_TASKS && currentIndex > 0) {
      currentIndex--;
      if (currentIndex < scrollOffset) {
        scrollOffset = currentIndex;
      }
    } else if (screenMode == MODE_TASKS) {
      currentIndex = taskCount - 1;
      scrollOffset = max(0, taskCount - VISIBLE_TASKS);
    }
    drawScreen();
  }

  // ── Periodic refresh ──────────────────────────────────────────────────
  static unsigned long lastFetch = 0;
  if (millis() - lastFetch > 30000) {
    if (wifiConnected) fetchTasks();
    drawScreen();
    lastFetch = millis();
  }

  // ── Fire deferred HTTP ────────────────────────────────────────────────
  if (pendingAction == PENDING_TOGGLE) {
    toggleTask(pendingId);
    pendingAction = NONE;
  } else if (pendingAction == PENDING_DELETE) {
    deleteTask(pendingId);
    pendingAction = NONE;
  }

  delay(50);
}

// ══════════════════════════════════════════════════════════════════════════
//  HTTP
// ══════════════════════════════════════════════════════════════════════════

void fetchTasks() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String("https://") + SERVER_HOST + "/api/tasks");
  http.setTimeout(5000);
  int code = http.GET();

  if (code == 200) {
    String json = http.getString();
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      taskCount = min((int)arr.size(), MAX_TASKS);
      for (int i = 0; i < taskCount; i++) {
        taskList[i].id = arr[i]["id"];
        taskList[i].done = arr[i]["done"];
        strncpy(taskList[i].title, arr[i]["title"] | "", MAX_TITLE - 1);
        taskList[i].title[MAX_TITLE - 1] = '\0';
      }
      if (currentIndex >= taskCount) {
        currentIndex = max(0, taskCount - 1);
        scrollOffset = max(0, currentIndex - (VISIBLE_TASKS - 1));
      }
    }
  }
  http.end();
}

void toggleTask(int id) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String("https://") + SERVER_HOST + "/api/tasks/" + id + "/toggle");
  http.setTimeout(5000);
  http.POST("");
  http.end();
}

void deleteTask(int id) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin(String("https://") + SERVER_HOST + "/api/tasks/" + id);
  http.setTimeout(5000);
  http.sendRequest("DELETE", "");
  http.end();
}

void deleteAllDone() {
  // Remove all done tasks locally
  int writeIdx = 0;
  for (int readIdx = 0; readIdx < taskCount; readIdx++) {
    if (taskList[readIdx].done) {
      // Fire delete for this id
      deleteTask(taskList[readIdx].id);
    } else {
      taskList[writeIdx++] = taskList[readIdx];
    }
  }
  taskCount = writeIdx;
  if (currentIndex >= taskCount) currentIndex = max(0, taskCount - 1);
}

// ══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ══════════════════════════════════════════════════════════════════════════

int countDone() {
  int n = 0;
  for (int i = 0; i < taskCount; i++) if (taskList[i].done) n++;
  return n;
}

// ══════════════════════════════════════════════════════════════════════════
//  DRAW SCREEN
// ══════════════════════════════════════════════════════════════════════════

void drawScreen() {
  M5.Lcd.fillScreen(BG_COLOR);

  // If shake confirm is pending, show that instead
  if (shakeConfirmPending) {
    drawShakeConfirm();
    return;
  }

  // Switch on mode
  if (screenMode == MODE_STATS) {
    drawStatsView();
  } else {
    drawTasksView();
  }
}

// ══════════════════════════════════════════════════════════════════════════
//  TASKS VIEW
// ══════════════════════════════════════════════════════════════════════════

void drawTasksView() {
  // Header (draw first so status bar overwrites overflow)
  M5.Lcd.setFont(&fonts::FreeSans12pt7b);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setCursor(10, HEADER_Y);
  M5.Lcd.println("Today");

  drawStatusBar();

  M5.Lcd.setFont(&fonts::FreeSans9pt7b);

  if (taskCount == 0) {
    M5.Lcd.setTextColor(DIM);
    M5.Lcd.setCursor(10, 60);
    M5.Lcd.print("No tasks yet");
    M5.Lcd.setCursor(10, 80);
    M5.Lcd.setTextColor(GRAY);
    M5.Lcd.print("Add at:");
    M5.Lcd.setCursor(10, 100);
    M5.Lcd.setTextColor(DIM);
    M5.Lcd.print(SERVER_HOST);
  }

  for (int i = scrollOffset; i < taskCount && i < scrollOffset + VISIBLE_TASKS; i++) {
    int y = LIST_START_Y + (i - scrollOffset) * LIST_ROW_H;
    bool sel = (i == currentIndex);

    if (taskList[i].done) {
      M5.Lcd.fillCircle(14, y + 6, 5, GREEN);
      if (sel) M5.Lcd.drawCircle(14, y + 6, 5, TFT_WHITE);
    } else {
      M5.Lcd.drawCircle(14, y + 6, 5, sel ? TFT_WHITE : GRAY);
    }

    if (taskList[i].done) {
      M5.Lcd.setTextColor(sel ? GREEN : DIM);
    } else {
      M5.Lcd.setTextColor(sel ? TFT_WHITE : GRAY);
    }

    M5.Lcd.setCursor(26, y);
    int len = strlen(taskList[i].title);
    if (len > 15) {
      char buf[17];
      memcpy(buf, taskList[i].title, 13);
      buf[13] = '.'; buf[14] = '.'; buf[15] = '\0';
      M5.Lcd.print(buf);
    } else {
      M5.Lcd.print(taskList[i].title);
    }
  }

  drawProgressBar();
}

// ══════════════════════════════════════════════════════════════════════════
//  STATS VIEW
// ══════════════════════════════════════════════════════════════════════════

void drawStatsView() {
  // In rotated mode (135x240 tall), use smaller fonts
  M5.Lcd.setFont(&fonts::Font0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.println("Stats");

  M5.Lcd.setTextSize(1);

  int done = countDone();
  int total = taskCount;
  int pct = total > 0 ? done * 100 / total : 0;

  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setCursor(10, 40);
  M5.Lcd.print("Tasks: ");
  M5.Lcd.setTextColor(GREEN);
  M5.Lcd.printf("%d/%d", done, total);

  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setCursor(10, 60);
  M5.Lcd.print("Rate:  ");
  M5.Lcd.setTextColor(pct >= 50 ? GREEN : GRAY);
  M5.Lcd.printf("%d%%", pct);

  int vol_per = M5.Power.getBatteryLevel();
  bool charging = M5.Power.isCharging();
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setCursor(10, 80);
  M5.Lcd.print("Batt:  ");
  M5.Lcd.setTextColor(vol_per > 20 ? GREEN : TFT_RED);
  M5.Lcd.printf("%d%%", vol_per);
  if (charging) {
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.print(" CHG");
  }

  // Big completion percentage
  M5.Lcd.setTextSize(4);
  M5.Lcd.setTextColor(pct >= 50 ? GREEN : GRAY);
  M5.Lcd.setCursor(25, 120);
  M5.Lcd.printf("%d%%", pct);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(GRAY);
  M5.Lcd.setCursor(25, 170);
  M5.Lcd.print("complete");

  // WiFi status
  M5.Lcd.setTextColor(GRAY);
  M5.Lcd.setCursor(10, 200);
  M5.Lcd.print("WiFi: ");
  M5.Lcd.setTextColor(wifiConnected ? GREEN : TFT_RED);
  M5.Lcd.print(wifiConnected ? "OK" : "OFF");
}

// ══════════════════════════════════════════════════════════════════════════
//  SHAKE CONFIRM
// ══════════════════════════════════════════════════════════════════════════

void drawShakeConfirm() {
  M5.Lcd.setFont(&fonts::FreeSans9pt7b);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setCursor(10, 30);
  M5.Lcd.print("Clear all done tasks?");

  M5.Lcd.setTextColor(GRAY);
  M5.Lcd.setCursor(10, 55);
  M5.Lcd.print("B: Yes    A: No");
}

// ══════════════════════════════════════════════════════════════════════════
//  STATUS BAR
// ══════════════════════════════════════════════════════════════════════════

void drawStatusBar() {
  M5.Lcd.fillRect(0, 0, 240, 16, BG_COLOR);

  M5.Lcd.setFont(&fonts::FreeSans9pt7b);
  if (wifiConnected) {
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setCursor(10, 1);
    M5.Lcd.print("●");
  }

  int vol_per = M5.Power.getBatteryLevel();
  bool charging = M5.Power.isCharging();

  int bx = 190, bw = 40, bh = 12;
  M5.Lcd.drawRect(bx, 2, bw, bh, GRAY);
  M5.Lcd.fillRect(bx + bw, 5, 3, bh - 6, GRAY);

  int fillW = constrain(vol_per, 0, 100) * (bw - 4) / 100;
  M5.Lcd.fillRect(bx + 2, 4, fillW, bh - 4, vol_per > 20 ? GREEN : TFT_RED);

  if (charging) {
    M5.Lcd.setTextColor(GREEN);
    M5.Lcd.setCursor(170, 1);
    M5.Lcd.print("⚡");
  }

  M5.Lcd.drawFastHLine(0, 16, 240, DIVIDER_COLOR);
}

// ══════════════════════════════════════════════════════════════════════════
//  PROGRESS BAR
// ══════════════════════════════════════════════════════════════════════════

void drawProgressBar() {
  if (taskCount == 0) return;

  int done = countDone();
  int pct = done * 100 / taskCount;

  M5.Lcd.fillRoundRect(10, PROGRESS_BAR_Y, 220, 8, 4, DIVIDER_COLOR);
  if (pct > 0) {
    int w = pct * 216 / 100;
    M5.Lcd.fillRoundRect(12, PROGRESS_BAR_Y + 1, w, 6, 3, GREEN);
  }

  M5.Lcd.setFont(&fonts::Font0);
  M5.Lcd.setTextColor(GRAY);
  M5.Lcd.setCursor(10, PROGRESS_BAR_Y + 14);
  M5.Lcd.printf("%d/%d done", done, taskCount);
}