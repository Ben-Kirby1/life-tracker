# Deploy Life Tracker Server to PythonAnywhere

PythonAnywhere's free tier keeps your SQLite database between restarts — data won't be wiped.

## Step-by-step

### 1. Create a PythonAnywhere account
- Go to [pythonanywhere.com](https://www.pythonanywhere.com)
- Sign up for a free "Beginner" account
- Your URL will be: `https://YOUR_USERNAME.pythonanywhere.com`

### 2. Open a Bash console
- In the dashboard, click **Bash** under the "Consoles" section
- Clone the repo:
```bash
git clone https://github.com/Ben-Kirby1/life-tracker.git
cd life-tracker/server
```

### 3. Create a virtualenv
```bash
mkvirtualenv life-tracker --python=/usr/bin/python3.12
pip install -r requirements.txt
```

### 4. Set up the web app
- Click **Web** in the top menu
- Click **Add a new web app**
- Choose **Manual configuration**, Python 3.12
- Click Next

### 5. Configure the WSGI file
- In the Web tab, find the **Code** section
- Click the link for **WSGI configuration file** (`/var/www/YOUR_USERNAME_pythonanywhere_com_wsgi.py`)
- Replace everything with:

```python
import sys
sys.path.insert(0, '/home/YOUR_USERNAME/life-tracker/server')
from task_api import app as application
```

- Replace `YOUR_USERNAME` with your actual PythonAnywhere username
- Click **Save**

### 6. Set the virtualenv path
- In the Web tab, find the **Virtualenv** section
- Enter: `/home/YOUR_USERNAME/.virtualenvs/life-tracker`
- Click the blue checkmark

### 7. Reload and test
- Click the green **Reload** button at the top of the Web tab
- Visit `https://YOUR_USERNAME.pythonanywhere.com/api/tasks` — should return `[]`
- Visit `https://YOUR_USERNAME.pythonanywhere.com/` — should show the web UI

## Update StickS3 firmware

In `firmware/life_tracker/life_tracker.ino`, change:
```cpp
const char* SERVER_HOST = "life-tracker-server-ipri.onrender.com";
```
to:
```cpp
const char* SERVER_HOST = "YOUR_USERNAME.pythonanywhere.com";
```

Then recompile and upload to the StickS3.

## Note
The free tier sleeps after 3 months of inactivity (not 15 minutes like Render — much better). Data is persistent. If it does sleep, one request wakes it in ~5 seconds.