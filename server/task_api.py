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
            position INTEGER DEFAULT 0,
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
        "SELECT id, title, done FROM tasks WHERE created = date('now') ORDER BY position, id"
    ).fetchall()
    conn.close()
    return jsonify([dict(r) for r in rows])

@app.route("/api/tasks", methods=["POST"])
def add_task():
    data = request.get_json()
    if not data or not data.get("title", "").strip():
        return jsonify({"error": "title required"}), 400
    conn = get_db()
    # Get max position for ordering
    max_pos = conn.execute("SELECT COALESCE(MAX(position), -1) FROM tasks WHERE created = date('now')").fetchone()[0]
    conn.execute("INSERT INTO tasks (title, position) VALUES (?, ?)", (data["title"].strip(), max_pos + 1))
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

@app.route("/api/tasks/<int:task_id>", methods=["DELETE"])
def delete_task(task_id):
    conn = get_db()
    conn.execute("DELETE FROM tasks WHERE id = ?", (task_id,))
    conn.commit()
    conn.close()
    return jsonify({"ok": True})

@app.route("/api/tasks/reorder", methods=["POST"])
def reorder_tasks():
    data = request.get_json()
    if not data or "order" not in data:
        return jsonify({"error": "order list required"}), 400
    conn = get_db()
    for i, task_id in enumerate(data["order"]):
        conn.execute("UPDATE tasks SET position = ? WHERE id = ?", (i, task_id))
    conn.commit()
    conn.close()
    return jsonify({"ok": True})

# ─── Web UI ───────────────────────────────────────────────────────────────

HTML_PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Life Tracker</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: #000;
    color: #fff;
    padding: 24px 20px;
    max-width: 420px;
    margin: auto;
    min-height: 100vh;
  }
  h1 {
    font-size: 1.6em;
    font-weight: 700;
    margin-bottom: 4px;
    letter-spacing: -0.5px;
  }
  .subtitle {
    color: #666;
    font-size: 0.85em;
    margin-bottom: 24px;
  }
  form {
    display: flex;
    gap: 10px;
    margin-bottom: 28px;
  }
  input[type=text] {
    flex: 1;
    padding: 14px 16px;
    border: none;
    border-radius: 12px;
    background: #1a1a1a;
    color: #fff;
    font-size: 1em;
    outline: none;
  }
  input[type=text]:focus {
    background: #222;
    outline: 2px solid #333;
  }
  input[type=text]::placeholder {
    color: #555;
  }
  button {
    padding: 14px 20px;
    border: none;
    border-radius: 12px;
    background: #007aff;
    color: white;
    font-size: 1em;
    font-weight: 600;
    cursor: pointer;
    transition: background 0.15s;
  }
  button:hover { background: #0056cc; }
  button:active { background: #004099; }
  .task-list { }
  .task-row {
    display: flex;
    align-items: center;
    padding: 10px 0;
    border-bottom: 1px solid #1a1a1a;
    gap: 12px;
    cursor: grab;
    touch-action: none;
    transition: opacity 0.15s, transform 0.15s;
  }
  .task-row.dragging {
    opacity: 0.4;
    transform: scale(0.95);
  }
  .task-row.drag-over {
    border-bottom: 2px solid #007aff;
  }
  .task-row .handle {
    color: #333;
    font-size: 0.8em;
    cursor: grab;
    padding: 4px;
    flex-shrink: 0;
  }
  .task-row .handle:hover {
    color: #555;
  }
  .task-row input[type=checkbox] {
    appearance: none;
    -webkit-appearance: none;
    width: 22px;
    height: 22px;
    border: 2px solid #444;
    border-radius: 50%;
    cursor: pointer;
    flex-shrink: 0;
    transition: all 0.15s;
    position: relative;
  }
  .task-row input[type=checkbox]:checked {
    border-color: #34c759;
    background: #34c759;
  }
  .task-row input[type=checkbox]:checked::after {
    content: '';
    position: absolute;
    left: 6px;
    top: 2px;
    width: 6px;
    height: 10px;
    border: solid #fff;
    border-width: 0 2px 2px 0;
    transform: rotate(45deg);
  }
  .task-row .title {
    flex: 1;
    font-size: 1em;
    color: #fff;
    transition: color 0.15s;
  }
  .task-row .title.done {
    color: #555;
    text-decoration: line-through;
  }
  .task-row .del {
    background: none;
    border: none;
    color: #333;
    font-size: 1.2em;
    cursor: pointer;
    padding: 4px 8px;
    border-radius: 8px;
    transition: all 0.15s;
  }
  .task-row .del:hover {
    color: #ff3b30;
    background: #1a0a0a;
  }
  .empty {
    text-align: center;
    padding: 60px 0;
    color: #444;
  }
  .empty .icon { font-size: 3em; margin-bottom: 12px; }
  .empty p { font-size: 0.9em; }
  .footer {
    margin-top: 32px;
    text-align: center;
    color: #222;
    font-size: 0.75em;
  }
</style>
</head>
<body>
<h1>Tracker</h1>
<p class="subtitle">Today's tasks</p>
<form id="addForm">
  <input type="text" id="taskInput" placeholder="New task..." autocomplete="off">
  <button type="submit">Add</button>
</form>
<div id="taskList" class="task-list"></div>
<div class="footer">Syncs to your StickS3</div>
<script>
async function loadTasks() {
  const r = await fetch('/api/tasks');
  const tasks = await r.json();
  const list = document.getElementById('taskList');
  if (tasks.length === 0) {
    list.innerHTML = '<div class="empty"><div class="icon">📋</div><p>No tasks yet</p></div>';
    return;
  }
  list.innerHTML = tasks.map(t => `
    <div class="task-row" draggable="true" data-id="${t.id}" ondragstart="onDragStart(event)" ondragover="onDragOver(event)" ondrop="onDrop(event)" ondragend="onDragEnd(event)" ontouchstart="onTouchStart(event, ${t.id})" ontouchmove="onTouchMove(event)" ontouchend="onTouchEnd(event)">
      <span class="handle">⠿</span>
      <input type="checkbox" ${t.done ? 'checked' : ''} onchange="toggle(${t.id})">
      <span class="title ${t.done ? 'done' : ''}">${t.title}</span>
      <button class="del" onclick="del(${t.id})">✕</button>
    </div>
  `).join('');
}
async function toggle(id) {
  await fetch('/api/tasks/' + id + '/toggle', { method: 'POST' });
  loadTasks();
}
async function del(id) {
  await fetch('/api/tasks/' + id, { method: 'DELETE' });
  loadTasks();
}
let dragId = null;
let touchStartY = 0;
let touchDragId = null;
function onDragStart(e) {
  dragId = e.target.closest('.task-row').dataset.id;
  e.target.closest('.task-row').classList.add('dragging');
  e.dataTransfer.effectAllowed = 'move';
}
function onDragOver(e) {
  e.preventDefault();
  e.dataTransfer.dropEffect = 'move';
  const row = e.target.closest('.task-row');
  if (row && row.dataset.id !== dragId) row.classList.add('drag-over');
}
function onDrop(e) {
  e.preventDefault();
  const target = e.target.closest('.task-row');
  if (target && dragId) saveOrder();
  document.querySelectorAll('.task-row').forEach(r => r.classList.remove('drag-over', 'dragging'));
  dragId = null;
}
function onDragEnd(e) {
  document.querySelectorAll('.task-row').forEach(r => r.classList.remove('drag-over', 'dragging'));
  dragId = null;
}
function onTouchStart(e, id) {
  touchStartY = e.touches[0].clientY;
  touchDragId = id;
}
function onTouchMove(e) {
  e.preventDefault();
}
function onTouchEnd(e) {
  if (touchDragId) saveOrder();
  touchDragId = null;
}
async function saveOrder() {
  const rows = document.querySelectorAll('.task-row');
  const order = Array.from(rows).map(r => parseInt(r.dataset.id));
  await fetch('/api/tasks/reorder', {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify({order: order})
  });
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
setInterval(loadTasks, 5000);  // auto-refresh every 5s
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
    print(f"  Web UI:  https://{os.environ.get('PA_USER', 'localhost')}.pythonanywhere.com")
    app.run(host="0.0.0.0", port=port, debug=True)