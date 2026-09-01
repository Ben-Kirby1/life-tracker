#include <M5Unified.h>
#include <utility/power/M5PM1_Class.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ─── WiFi Configuration ──────────────────────────────────────────────────
// Set these to your home WiFi and the IP of the computer running the Flask server
const char* WIFI_SSID = "University House Midtown";
const char* WIFI_PASS = "Cat-Lime~Ragdoll";
const char* SERVER_HOST = "life-tracker-server-ipri.onrender.com";  // HTTPS, no port needed

// ─── Task Data ───────────────────────────────────────────────────────────
#define MAX_TASKS 10
#define MAX_TITLE 40

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

// ─── Screen Layout ───────────────────────────────────────────────────────
#define TITLE_Y        5
#define LIST_START_Y   22
#define LIST_ROW_H     12
#define STATUS_Y       85
#define BATTERY_Y      105
#define CHARGE_Y       120

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
  M5.Lcd.fillScreen(TFT_BLACK);

  // Connect to WiFi
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_CYAN);
  M5.Lcd.setCursor(10, 30);
  M5.Lcd.print("Connecting to:");
  M5.Lcd.setCursor(10, 42);
  M5.Lcd.print(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  int attempts = 0;
  int lastStatus = -1;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    M5.Lcd.setCursor(10, 55);
    M5.Lcd.printf("Attempt %d/40", attempts + 1);
    attempts++;
    
    int s = WiFi.status();
    if (s != lastStatus) {
      M5.Lcd.setCursor(10, 68);
      M5.Lcd.fillRect(10, 68, 220, 40, TFT_BLACK);
      M5.Lcd.printf("Status: %d", s);
      lastStatus = s;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    M5.Lcd.fillRect(0, 20, 240, 60, TFT_BLACK);
    M5.Lcd.setTextColor(TFT_GREEN);
    M5.Lcd.setCursor(10, 35);
    M5.Lcd.print("WiFi OK!");
    M5.Lcd.setCursor(10, 50);
    M5.Lcd.print(WiFi.localIP());
    fetchTasks();
  } else {
    M5.Lcd.fillRect(0, 20, 240, 80, TFT_BLACK);
    M5.Lcd.setTextColor(TFT_RED);
    M5.Lcd.setCursor(10, 35);
    M5.Lcd.print("WiFi FAILED");
    M5.Lcd.setCursor(10, 50);
    int s = WiFi.status();
    M5.Lcd.printf("Code: %d", s);
    M5.Lcd.setCursor(10, 62);
    if (s == 1) M5.Lcd.print("= No SSID found");
    else if (s == 4) M5.Lcd.print("= Connection failed");
    else M5.Lcd.print("= Check 2.4GHz band");
  }

  delay(1500);
  drawScreen();
}

void loop() {
  M5.update();

  // Button A: scroll down
  if (M5.BtnA.wasPressed()) {
    if (currentIndex < taskCount - 1) {
      currentIndex++;
      int maxVisible = 4;
      if (currentIndex - scrollOffset >= maxVisible) {
        scrollOffset = currentIndex - maxVisible + 1;
      }
    } else {
      currentIndex = 0;
      scrollOffset = 0;
    }
    drawScreen();
  }

  // Button B: tap = toggle, hold = scroll up
  if (M5.BtnB.wasPressed()) {
    if (taskCount > 0 && currentIndex < taskCount) {
      toggleTask(taskList[currentIndex].id);
      delay(200);
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
      scrollOffset = taskCount - 4;
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

  // Refresh battery every 10 seconds
  static unsigned long lastBatt = 0;
  if (millis() - lastBatt > 10000) {
    drawBattery();
    lastBatt = millis();
  }

  delay(50);
}

// ─── HTTP ────────────────────────────────────────────────────────────────

void fetchTasks() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  // For Render (cloud): use HTTPS, no port
  String url = String("https://") + SERVER_HOST + "/api/tasks";
  #pragma message "Using HTTPS connection for Render"

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

      // Clamp current index
      if (currentIndex >= taskCount) {
        currentIndex = max(0, taskCount - 1);
        scrollOffset = max(0, currentIndex - 3);
      }
    }
  }

  http.end();
}

void toggleTask(int id) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  // For Render (cloud): use HTTPS, no port
  String url = String("https://") + SERVER_HOST + "/api/tasks/" + id + "/toggle";

  http.begin(url);
  http.setTimeout(5000);
  http.POST("");  // empty body, just triggers the POST
  http.end();
}

// ─── Display ─────────────────────────────────────────────────────────────

void drawScreen() {
  M5.Lcd.fillScreen(TFT_BLACK);

  // Title
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE);
  M5.Lcd.setCursor(10, TITLE_Y);
  M5.Lcd.println("Tracker");

  if (!wifiConnected) {
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_RED);
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.println("No WiFi — holding last data");
  }

  // Task list
  M5.Lcd.setTextSize(1);
  int visible = 4;

  if (taskCount == 0) {
    M5.Lcd.setTextColor(TFT_DARKGREY);
    M5.Lcd.setCursor(10, 40);
    M5.Lcd.println("Add tasks on your phone:");
    M5.Lcd.setCursor(10, 52);
    M5.Lcd.printf("https://%s", SERVER_HOST);
  }

  for (int i = scrollOffset; i < taskCount && i < scrollOffset + visible; i++) {
    int y = LIST_START_Y + (i - scrollOffset) * LIST_ROW_H;

    // Highlight selected row
    if (i == currentIndex) {
      M5.Lcd.fillRoundRect(5, y - 1, 230, LIST_ROW_H + 1, 2, TFT_NAVY);
    }

    if (taskList[i].done) {
      M5.Lcd.setTextColor(TFT_GREEN);
      M5.Lcd.setCursor(10, y);
      M5.Lcd.print("[x] ");
    } else {
      M5.Lcd.setTextColor(i == currentIndex ? TFT_CYAN : TFT_WHITE);
      M5.Lcd.setCursor(10, y);
      M5.Lcd.print("[ ] ");
    }

    M5.Lcd.print(taskList[i].title);
  }

  // Scroll indicators
  if (scrollOffset > 0) {
    M5.Lcd.setTextColor(TFT_DARKGREY);
    M5.Lcd.setCursor(220, LIST_START_Y);
    M5.Lcd.print("^");
  }
  if (scrollOffset + visible < taskCount) {
    M5.Lcd.setTextColor(TFT_DARKGREY);
    M5.Lcd.setCursor(220, 70);
    M5.Lcd.print("v");
  }

  // Status line
  M5.Lcd.setTextSize(1);
  if (wifiConnected) {
    M5.Lcd.setTextColor(TFT_WHITE);
    M5.Lcd.setCursor(10, STATUS_Y);
    M5.Lcd.printf("%d/%d  A:v", doneCount, taskCount);
    M5.Lcd.setTextColor(TFT_DARKGREY);
    M5.Lcd.setCursor(90, STATUS_Y);
    M5.Lcd.print("B:tap=x hold=^");
  } else {
    M5.Lcd.setTextColor(TFT_RED);
    M5.Lcd.setCursor(10, STATUS_Y);
    M5.Lcd.print("WiFi disconnected");
  }

  drawBattery();
}

void drawBattery() {
  M5.Lcd.fillRect(0, BATTERY_Y, 240, 25, TFT_BLACK);

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_YELLOW);
  M5.Lcd.setCursor(10, BATTERY_Y);

  int vol_per = M5.Power.getBatteryLevel();
  int vol = M5.Power.getBatteryVoltage();
  bool charging = M5.Power.isCharging();

  M5.Lcd.printf("Bat: %d%%  %dmV", vol_per, vol);

  M5.Lcd.setCursor(10, CHARGE_Y);
  if (charging) {
    M5.Lcd.setTextColor(TFT_GREEN);
    M5.Lcd.print("CHARGING");
  } else {
    M5.Lcd.setTextColor(TFT_ORANGE);
    M5.Lcd.print("DISCHARGING");
  }
}