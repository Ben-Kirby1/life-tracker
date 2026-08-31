# Deploy to Render

1. Go to [dashboard.render.com](https://dashboard.render.com) and sign up (GitHub login works)
2. Click **New + → Web Service**
3. Connect your GitHub account, select `life-tracker`
4. Fill in:
   - **Name:** `life-tracker-server`
   - **Root Directory:** `server`
   - **Build Command:** `pip install -r requirements.txt`
   - **Start Command:** `gunicorn task_api:app`
   - **Plan:** Free
5. Click **Deploy Web Service**
6. Wait ~2 minutes — you'll get a URL like `https://life-tracker-server.onrender.com`
7. Copy that URL — update the StickS3 firmware with it

## Update StickS3 firmware

In `firmware/life_tracker/life_tracker.ino`, change:
```cpp
const char* WIFI_SSID = "your_wifi_name";           
const char* WIFI_PASS = "your_wifi_password";        
const char* SERVER_HOST = "192.168.1.100";  // → change to:
const char* SERVER_HOST = "life-tracker-server.onrender.com";  // no http://, no port
```

Also in `fetchTasks()` and `toggleTask()`, remove the `SERVER_PORT` — Render uses port 443 (HTTPS).

```cpp
// Change from:
String url = String("http://") + SERVER_HOST + ":" + SERVER_PORT + "/api/tasks";
// To:
String url = String("https://") + SERVER_HOST + "/api/tasks";
```

Then upload the firmware to StickS3.