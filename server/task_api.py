#!/usr/bin/env python3
"""
Life Tracker — Flask Task API
==============================
Run:  python task_api.py
Then open:  http://localhost:5000  (or your computer's IP on your phone)

Endpoints:
  GET  /api/tasks     → JSON list of today's tasks
  POST /api/tasks     → add a task {"title": "do thing"}
  POST /api/tasks/<id>/toggle → toggle done/undone
  GET  /              → web UI for adding tasks from your phone
"""
import json
import sqlite3
import os
from datetime import date
from flask import Flask, request, jsonify

app = Flask(__name__)
DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tasks.db")

# ─── Database ─────────────────────────────────────────────────────────────

def get_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS tasks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            done INTEGER DEFAULT 0,
            created TEXT DEFAULT (date('now'))
        )
    """)
    # Clean up old tasks (keep today's)
    conn.execute("DELETE FROM tasks WHERE created != date('now') AND done = 1")
    conn.commit()
    return conn

# ─── API ──────────────────────────────────────────────────────────────────

@app.route("/api/tasks", methods=["GET"])
def list_tasks():
    conn = get_db()
    rows = conn.execute(
        "SELECT id, title, done FROM tasks WHERE created = date('now') ORDER BY id"
    ).fetchall()
    conn.close()
    return jsonify([dict(r) for r in rows])

@app.route("/api/tasks", methods=["POST"])
def add_task():
    data = request.get_json()
    if not data or not data.get("title", "").strip():
        return jsonify({"error": "title required"}), 400
    conn = get_db()
    conn.execute("INSERT INTO tasks (title) VALUES (?)", (data["title"].strip(),))
    conn.commit()
    task_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0]
    conn.close()
    return jsonify({"id": task_id, "title": data["title"].strip(), "done": 0}), 201

@app.route("/api/tasks/<int:task_id>/toggle", methods=["POST"])
def toggle_task(task_id):
    conn = get_db()
    conn.execute("UPDATE tasks SET done = CASE WHEN done THEN 0 ELSE 1 END WHERE id = ?", (task_id,))
    conn.commit()
    task = conn.execute("SELECT id, title, done FROM tasks WHERE id = ?", (task_id,)).fetchone()
    conn.close()
    if task:
        return jsonify(dict(task))
    return jsonify({"error": "not found"}), 404

# ─── Web UI ───────────────────────────────────────────────────────────────

HTML_PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Life Tracker</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { font-family: -apple-system, sans-serif; background: #111; color: #eee; padding: 20px; max-width: 400px; margin: auto; }
  h1 { font-size: 1.4em; margin-bottom: 15px; }
  .task-row { display: flex; align-items: center; padding: 8px 0; border-bottom: 1px solid #333; }
  .task-row input { margin-right: 10px; }
  .task-done { text-decoration: line-through; color: #666; }
  form { display: flex; gap: 8px; margin-bottom: 20px; }
  input[type=text] { flex: 1; padding: 10px; border: 1px solid #444; border-radius: 6px; background: #222; color: #eee; font-size: 1em; }
  button { padding: 10px 16px; border: none; border-radius: 6px; background: #2196F3; color: white; font-size: 1em; cursor: pointer; }
  button:hover { background: #1976D2; }
</style>
</head>
<body>
<h1>📋 Life Tracker</h1>
<form id="addForm">
  <input type="text" id="taskInput" placeholder="Add a task..." autocomplete="off">
  <button type="submit">Add</button>
</form>
<div id="taskList"></div>
<script>
async function loadTasks() {
  const r = await fetch('/api/tasks');
  const tasks = await r.json();
  const list = document.getElementById('taskList');
  list.innerHTML = tasks.map(t => `
    <div class="task-row">
      <input type="checkbox" ${t.done ? 'checked' : ''} onchange="toggle(${t.id})">
      <span class="${t.done ? 'task-done' : ''}">${t.title}</span>
    </div>
  `).join('');
}
async function toggle(id) {
  await fetch('/api/tasks/' + id + '/toggle', { method: 'POST' });
  loadTasks();
}
document.getElementById('addForm').onsubmit = async (e) => {
  e.preventDefault();
  const input = document.getElementById('taskInput');
  if (!input.value.trim()) return;
  await fetch('/api/tasks', { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({title: input.value}) });
  input.value = '';
  loadTasks();
};
loadTasks();
</script>
</body>
</html>"""

@app.route("/")
def index():
    return HTML_PAGE

# ─── Main ──────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    port = int(os.environ.get("PORT", 5000))
    print(f"Life Tracker server starting on port {port}...")
    print(f"  API:     /api/tasks")
    print("  Press Ctrl+C to stop")
    app.run(host="0.0.0.0", port=port, debug=True)