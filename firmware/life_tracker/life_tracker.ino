#include <M5Unified.h>
#include <utility/power/M5PM1_Class.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ─── WiFi Configuration ──────────────────────────────────────────────────
const char* WIFI_SSID = "5G University House Midtown";
const char* WIFI_PASS = "your_wifi_password";
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

// ─── Screen Layout (135x240 portrait) ────────────────────────────────────
// The StickS3 is rotated so the 240-wide dimension is horizontal.
// We use the full 240x135 in landscape mode.
//
// Layout:
//   0-14   → Status bar (WiFi icon, time placeholder, battery)
//   16-26  → Large "Today" header
//   28-108 → Task list (7 rows of ~11px each)
//   110-120 → Progress bar
//   122-135 → Controls hint

#define STATUS_BAR_Y   0
#define HEADER_Y       16
#define LIST_START_Y   30
#define LIST_ROW_H     11
#define PROGRESS_BAR_Y 110
#define HINT_Y         122

// ─── Colors ──────────────────────────────────────────────────────────────
#define BG_COLOR       TFT_BLACK
#define CARD_COLOR     0x0841      // very dark gray-blue
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
      int maxVisible = 7;
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
      deleteTask(taskList[currentIndex].id);
      delay(300);
      fetchTasks();
      drawScreen();
    }
  }

  // Button B: tap = toggle, hold = scroll up
  if (M5.BtnB.wasPressed()) {
    if (taskCount > 0 && currentIndex < taskCount) {
      toggleTask(taskList[currentIndex].id);
      delay(300);
      fetchTasks();
      drawScreen();
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
      scrollOffset = taskCount - 7;
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

  delay(50);
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
        scrollOffset = max(0, currentIndex - 6);
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

  drawStatusBar();

  // Header
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TEXT_PRIMARY);
  M5.Lcd.setCursor(10, HEADER_Y);
  M5.Lcd.println("Today");

  // Task list
  M5.Lcd.setTextSize(1);
  int visible = 7;

  if (taskCount == 0) {
    // Empty state
    M5.Lcd.setTextColor(TEXT_DIM);
    M5.Lcd.setCursor(10, 60);
    M5.Lcd.println("No tasks yet");
    M5.Lcd.setCursor(10, 74);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TEXT_SECONDARY);
    M5.Lcd.print("Add at:");
    M5.Lcd.setCursor(10, 86);
    M5.Lcd.setTextColor(TEXT_DIM);
    M5.Lcd.setCursor(10, 86);
    M5.Lcd.print(SERVER_HOST);
  }

  for (int i = scrollOffset; i < taskCount && i < scrollOffset + visible; i++) {
    int y = LIST_START_Y + (i - scrollOffset) * LIST_ROW_H;

    // Draw the dot indicator
    if (taskList[i].done) {
      // Filled green circle
      M5.Lcd.fillCircle(14, y + 4, 4, DOT_DONE);
    } else {
      // Outlined circle — white if selected, gray otherwise
      uint16_t color = (i == currentIndex) ? DOT_SELECTED : DOT_ACTIVE;
      M5.Lcd.drawCircle(14, y + 4, 4, color);
    }

    // Task title
    if (taskList[i].done) {
      M5.Lcd.setTextColor(TEXT_DIM);
    } else if (i == currentIndex) {
      M5.Lcd.setTextColor(TEXT_PRIMARY);
    } else {
      M5.Lcd.setTextColor(TEXT_SECONDARY);
    }

    M5.Lcd.setCursor(24, y);

    // Truncate title if too long
    char buf[MAX_TITLE + 3];
    int len = strlen(taskList[i].title);
    if (len > 20) {
      memcpy(buf, taskList[i].title, 18);
      buf[18] = '.'; buf[19] = '.'; buf[20] = '\0';
      M5.Lcd.print(buf);
    } else {
      M5.Lcd.print(taskList[i].title);
    }
  }

  // Progress bar
  drawProgressBar();

  // Controls hint
  drawHint();
}

void drawStatusBar() {
  // Left side: WiFi indicator
  M5.Lcd.setTextSize(1);
  if (wifiConnected) {
    M5.Lcd.setTextColor(ACCENT_COLOR);
    M5.Lcd.setCursor(10, STATUS_BAR_Y + 2);
    M5.Lcd.print("●");  // WiFi dot
  }

  // Right side: battery
  int vol_per = M5.Power.getBatteryLevel();
  bool charging = M5.Power.isCharging();

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TEXT_SECONDARY);
  M5.Lcd.setCursor(180, STATUS_BAR_Y + 2);

  if (charging) {
    M5.Lcd.setTextColor(ACCENT_COLOR);
    M5.Lcd.print("⚡");
  }
  M5.Lcd.printf("%d%%", vol_per);

  // Thin separator line
  M5.Lcd.drawFastHLine(0, 14, 240, 0x2104);
}

void drawProgressBar() {
  if (taskCount == 0) return;

  int pct = (doneCount * 100) / taskCount;

  // Background track
  M5.Lcd.fillRoundRect(10, PROGRESS_BAR_Y, 220, 6, 3, 0x2104);

  // Fill
  if (pct > 0) {
    int fillW = map(pct, 0, 100, 0, 216);
    M5.Lcd.fillRoundRect(12, PROGRESS_BAR_Y + 1, fillW, 4, 2, ACCENT_COLOR);
  }

  // Text
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TEXT_SECONDARY);
  M5.Lcd.setCursor(10, PROGRESS_BAR_Y + 10);
  M5.Lcd.printf("%d/%d done", doneCount, taskCount);
}

void drawHint() {
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TEXT_DIM);
  M5.Lcd.setCursor(10, HINT_Y + 5);
  M5.Lcd.print("A:v  Ahold:del  B:~  Bhold:^");
}