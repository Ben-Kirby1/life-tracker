#include <M5Unified.h>
#include <utility/power/M5PM1_Class.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ─── WiFi Configuration ──────────────────────────────────────────────────
const char* WIFI_SSID = "University House Midtown";
const char* WIFI_PASS = "Cat-Lime~Ragdoll";
const char* SERVER_HOST = "life-tracker-server-ipri.onrender.com";  // HTTPS, no port needed

// ─── Task Data ───────────────────────────────────────────────────────────
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
int doneCount = 0;
bool wifiConnected = false;

// ─── Pending HTTP Actions (non-blocking) ─────────────────────────────────
enum PendingAction { NONE, PENDING_TOGGLE, PENDING_DELETE };
PendingAction pendingAction = NONE;
int pendingId = 0;

// ─── Screen Layout (135x240 portrait) ────────────────────────────────────
// The StickS3 is rotated so the 240-wide dimension is horizontal.
// We use the full 240x135 in landscape mode.
//
// Layout:
//   0-16   → Status bar (WiFi icon, battery bar)
//   18-30  → Large "Today" header
//   38-91  → Task list (3 rows of ~17px each)
//   96-104 → Progress bar
//   110-135 → Progress text

#define STATUS_BAR_Y   0
#define HEADER_Y       18
#define LIST_START_Y   40
#define LIST_ROW_H     17
// Progress bar
#define PROGRESS_BAR_Y 96

// ─── Fonts ───────────────────────────────────────────────────────────────
// Use larger fonts for readability
#define FONT_LARGE  &fonts::FreeSans12pt7b    // "Today" header
#define FONT_SMALL  &fonts::FreeSans9pt7b     // Task list
#define FONT_TINY   &fonts::FreeSans9pt7b     // Status bar, progress

// ─── Colors ──────────────────────────────────────────────────────────────
#define BG_COLOR       TFT_BLACK
#define ACCENT_COLOR   0x07E0      // green
#define TEXT_PRIMARY   0xFFFF      // white
#define TEXT_SECONDARY 0x8410      // gray
#define TEXT_DIM       0x4208      // dim gray
#define DOT_DONE       0x07E0      // green
#define DOT_ACTIVE     0x8410      // gray
#define DOT_SELECTED   0xFFFF      // white

void setup() {
  M5.begin();
  M5.Lcd.setRotation(1);

  // Disable speaker whine
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
  M5.Lcd.fillScreen(BG_COLOR);

  // Connect to WiFi
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TEXT_SECONDARY);
  M5.Lcd.setCursor(10, 50);
  M5.Lcd.print("Connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    M5.Lcd.setCursor(10, 62);
    M5.Lcd.printf("Attempt %d/40", attempts + 1);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    fetchTasks();
  }

  // Remove splash — draw the real UI
  drawScreen();
}

void loop() {
  M5.update();

  // Button A: short = scroll down, long = delete current task
  if (M5.BtnA.wasPressed()) {
    if (currentIndex < taskCount - 1) {
      currentIndex++;
      int maxVisible = 3;
      if (currentIndex - scrollOffset >= maxVisible) {
        scrollOffset = currentIndex - maxVisible + 1;
      }
    } else {
      currentIndex = 0;
      scrollOffset = 0;
    }
    drawScreen();
  }
  if (M5.BtnA.wasHold()) {
    if (taskCount > 0 && currentIndex < taskCount) {
      // Delete locally first — instant
      int id = taskList[currentIndex].id;
      for (int i = currentIndex; i < taskCount - 1; i++) {
        taskList[i] = taskList[i + 1];
      }
      taskCount--;
      if (currentIndex >= taskCount) currentIndex = max(0, taskCount - 1);
      drawScreen();
      // Then fire the HTTP call in the background
      pendingAction = PENDING_DELETE;
      pendingId = id;
    }
  }

  // Button B: tap = toggle, hold = scroll up
  if (M5.BtnB.wasPressed()) {
    if (taskCount > 0 && currentIndex < taskCount) {
      // Toggle locally first — instant
      taskList[currentIndex].done = !taskList[currentIndex].done;
      doneCount += taskList[currentIndex].done ? 1 : -1;
      drawScreen();
      // Then fire the HTTP call in the background
      pendingAction = PENDING_TOGGLE;
      pendingId = taskList[currentIndex].id;
    }
  }
  if (M5.BtnB.wasHold()) {
    if (currentIndex > 0) {
      currentIndex--;
      if (currentIndex < scrollOffset) {
        scrollOffset = currentIndex;
      }
    } else {
      currentIndex = taskCount - 1;
      scrollOffset = taskCount - 3;
      if (scrollOffset < 0) scrollOffset = 0;
    }
    drawScreen();
  }

  // Refresh every 30 seconds
  static unsigned long lastFetch = 0;
  if (millis() - lastFetch > 30000) {
    if (wifiConnected) fetchTasks();
    drawScreen();
    lastFetch = millis();
  }

  // Process pending HTTP action (non-blocking — runs after loop has polled buttons)
  if (pendingAction == PENDING_TOGGLE) {
    toggleTask(pendingId);
    pendingAction = NONE;
  } else if (pendingAction == PENDING_DELETE) {
    deleteTask(pendingId);
    pendingAction = NONE;
  }
}

// ─── HTTP ────────────────────────────────────────────────────────────────

void fetchTasks() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String("https://") + SERVER_HOST + "/api/tasks";
  http.begin(url);
  http.setTimeout(5000);
  int code = http.GET();

  if (code == 200) {
    String json = http.getString();
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, json);

    if (!err && doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      taskCount = min((int)arr.size(), MAX_TASKS);
      doneCount = 0;

      for (int i = 0; i < taskCount; i++) {
        taskList[i].id = arr[i]["id"];
        taskList[i].done = arr[i]["done"];
        strncpy(taskList[i].title, arr[i]["title"] | "", MAX_TITLE - 1);
        taskList[i].title[MAX_TITLE - 1] = '\0';
        if (taskList[i].done) doneCount++;
      }

      if (currentIndex >= taskCount) {
        currentIndex = max(0, taskCount - 1);
        scrollOffset = max(0, currentIndex - 2);
      }
    }
  }
  http.end();
}

void toggleTask(int id) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String("https://") + SERVER_HOST + "/api/tasks/" + id + "/toggle";
  http.begin(url);
  http.setTimeout(5000);
  http.POST("");
  http.end();
}

void deleteTask(int id) {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String("https://") + SERVER_HOST + "/api/tasks/" + id;
  http.begin(url);
  http.setTimeout(5000);
  http.sendRequest("DELETE", "");
  http.end();
}

// ─── Display ─────────────────────────────────────────────────────────────

void drawScreen() {
  M5.Lcd.fillScreen(BG_COLOR);

  // Header — draw this FIRST so it goes under the status bar
  M5.Lcd.setFont(FONT_LARGE);
  M5.Lcd.setTextColor(TEXT_PRIMARY);
  M5.Lcd.setCursor(10, HEADER_Y);
  M5.Lcd.println("Today");

  // Status bar — draw AFTER header so it overwrites any font overflow
  drawStatusBar();

  // Task list
  M5.Lcd.setFont(FONT_SMALL);
  int visible = 3;

  if (taskCount == 0) {
    // Empty state
    M5.Lcd.setTextColor(TEXT_DIM);
    M5.Lcd.setCursor(10, 60);
    M5.Lcd.print("No tasks yet");
    M5.Lcd.setCursor(10, 80);
    M5.Lcd.setTextColor(TEXT_SECONDARY);
    M5.Lcd.print("Add at:");
    M5.Lcd.setCursor(10, 100);
    M5.Lcd.setTextColor(TEXT_DIM);
    M5.Lcd.print(SERVER_HOST);
  }

  for (int i = scrollOffset; i < taskCount && i < scrollOffset + visible; i++) {
    int y = LIST_START_Y + (i - scrollOffset) * LIST_ROW_H;

    // Draw the dot indicator — larger for visibility
    bool isSelected = (i == currentIndex);
    if (taskList[i].done) {
      if (isSelected) {
        // Selected + done: bright green fill with white ring
        M5.Lcd.fillCircle(14, y + 6, 5, DOT_DONE);
        M5.Lcd.drawCircle(14, y + 6, 5, TFT_WHITE);
      } else {
        M5.Lcd.fillCircle(14, y + 6, 5, DOT_DONE);
      }
    } else {
      uint16_t color = isSelected ? DOT_SELECTED : DOT_ACTIVE;
      M5.Lcd.drawCircle(14, y + 6, 5, color);
    }

    // Task title
    if (taskList[i].done) {
      M5.Lcd.setTextColor(isSelected ? TFT_GREEN : TEXT_DIM);
    } else if (i == currentIndex) {
      M5.Lcd.setTextColor(TEXT_PRIMARY);
    } else {
      M5.Lcd.setTextColor(TEXT_SECONDARY);
    }

    M5.Lcd.setCursor(26, y);

    // Truncate title if needed (fewer chars with bigger font)
    char buf[MAX_TITLE + 3];
    int len = strlen(taskList[i].title);
    if (len > 15) {
      memcpy(buf, taskList[i].title, 13);
      buf[13] = '.'; buf[14] = '.'; buf[15] = '\0';
      M5.Lcd.print(buf);
    } else {
      M5.Lcd.print(taskList[i].title);
    }
  }

  // Progress bar
  drawProgressBar();
}

void drawStatusBar() {
  // Clear the entire status bar area first to prevent ghosting
  M5.Lcd.fillRect(0, 0, 240, 16, BG_COLOR);
  
  // Left side: WiFi indicator
  M5.Lcd.setFont(FONT_TINY);
  if (wifiConnected) {
    M5.Lcd.setTextColor(ACCENT_COLOR);
    M5.Lcd.setCursor(10, STATUS_BAR_Y + 1);
    M5.Lcd.print("●");
  }

  // Right side: battery bar
  int vol_per = M5.Power.getBatteryLevel();
  bool charging = M5.Power.isCharging();

  // Draw battery icon outline — bigger
  int bx = 190, by = 2, bw = 40, bh = 12;
  M5.Lcd.drawRect(bx, by, bw, bh, TEXT_SECONDARY);
  M5.Lcd.fillRect(bx + bw, by + 3, 3, bh - 6, TEXT_SECONDARY);

  // Fill based on level
  int fillW = map(constrain(vol_per, 0, 100), 0, 100, 0, bw - 4);
  uint16_t batColor = vol_per > 20 ? ACCENT_COLOR : TFT_RED;
  if (vol_per > 0) {
    M5.Lcd.fillRect(bx + 2, by + 2, fillW, bh - 4, batColor);
  }

  // Charging indicator
  if (charging) {
    M5.Lcd.setFont(FONT_TINY);
    M5.Lcd.setTextColor(ACCENT_COLOR);
    M5.Lcd.setCursor(170, STATUS_BAR_Y + 1);
    M5.Lcd.print("⚡");
  }

  // Thin separator line
  M5.Lcd.drawFastHLine(0, 16, 240, 0x2104);
}

void drawProgressBar() {
  if (taskCount == 0) return;

  int pct = (doneCount * 100) / taskCount;

  // Background track
  M5.Lcd.fillRoundRect(10, PROGRESS_BAR_Y, 220, 8, 4, 0x2104);

  // Fill
  if (pct > 0) {
    int fillW = map(pct, 0, 100, 0, 216);
    M5.Lcd.fillRoundRect(12, PROGRESS_BAR_Y + 1, fillW, 6, 3, ACCENT_COLOR);
  }

  // Text
  M5.Lcd.setFont(&fonts::Font0);
  M5.Lcd.setTextColor(TEXT_SECONDARY);
  M5.Lcd.setCursor(10, PROGRESS_BAR_Y + 14);
  M5.Lcd.printf("%d/%d done", doneCount, taskCount);
  // Clear any leftover text below
  M5.Lcd.fillRect(0, PROGRESS_BAR_Y + 24, 240, 20, BG_COLOR);
}