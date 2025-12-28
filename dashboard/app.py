#!/usr/bin/env python3
"""
Edge-Cloud Migration Dashboard
Real-time monitoring and control panel for the migration system
"""

from flask import Flask, render_template, jsonify, request
import json
import os
import subprocess
import threading
import time
from datetime import datetime

app = Flask(__name__)

# State tracking
state = {
    "task_name": None,
    "progress": 0,
    "location": "idle",  # idle, edge, cloud, migrating
    "telemetry": {"cpu": 0, "mem": 0, "temp": 0, "battery": 100},
    "history": [],
    "edge_pid": None,
    "cloud_pid": None
}

def read_telemetry():
    """Read current telemetry from system or files"""
    try:
        # Try to read from proc (Linux)
        with open('/proc/loadavg', 'r') as f:
            load = float(f.read().split()[0])
            cores = os.cpu_count() or 1
            state["telemetry"]["cpu"] = min(100, (load / cores) * 100)
    except:
        state["telemetry"]["cpu"] = 50  # Simulation

def read_task_status():
    """Read task status from files"""
    json_file = "/tmp/car_detect_internal.json"
    if os.path.exists(json_file):
        try:
            with open(json_file, 'r') as f:
                data = json.load(f)
                state["progress"] = data.get("progress", 0)
                state["task_name"] = "car_detect"
        except:
            pass
    
    # Determine location
    if os.path.exists("/tmp/task_state.bin") and not os.path.exists("/tmp/edge_return.bin"):
        state["location"] = "cloud"
    elif state["progress"] > 0 and state["progress"] < 100:
        state["location"] = "edge"
    elif state["progress"] >= 100:
        state["location"] = "completed"
    else:
        state["location"] = "idle"

@app.route('/')
def index():
    return render_template('dashboard.html')

@app.route('/api/status')
def api_status():
    read_telemetry()
    read_task_status()
    return jsonify(state)

@app.route('/api/start', methods=['POST'])
def api_start():
    """Start a new task"""
    # Clean up old state
    for f in ["/tmp/task_state.bin", "/tmp/edge_return.bin", 
              "/tmp/car_detect_internal.json", "/tmp/cloud_finished"]:
        try:
            os.remove(f)
        except:
            pass
    
    # Start cloud runtime in background
    cloud_proc = subprocess.Popen(
        ["./cloud/cloud_runtime"],
        cwd="/Users/nadigsrujan/Documents/osel",
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    state["cloud_pid"] = cloud_proc.pid
    time.sleep(1)
    
    # Start edge runtime in background  
    edge_proc = subprocess.Popen(
        ["./edge/edge_runtime"],
        cwd="/Users/nadigsrujan/Documents/osel",
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    state["edge_pid"] = edge_proc.pid
    
    state["location"] = "edge"
    state["progress"] = 0
    state["history"].append({
        "time": datetime.now().strftime("%H:%M:%S"),
        "event": "Task started on Edge"
    })
    
    return jsonify({"status": "started"})

@app.route('/api/stop', methods=['POST'])
def api_stop():
    """Stop current task"""
    subprocess.run(["pkill", "-f", "edge_runtime"], capture_output=True)
    subprocess.run(["pkill", "-f", "cloud_runtime"], capture_output=True)
    subprocess.run(["pkill", "-f", "car_detect"], capture_output=True)
    
    state["location"] = "idle"
    state["progress"] = 0
    state["history"].append({
        "time": datetime.now().strftime("%H:%M:%S"),
        "event": "Task stopped"
    })
    
    return jsonify({"status": "stopped"})

@app.route('/api/history')
def api_history():
    return jsonify(state["history"][-20:])  # Last 20 events

if __name__ == '__main__':
    print("=" * 50)
    print("  Edge-Cloud Migration Dashboard")
    print("  Open http://localhost:5050 in your browser")
    print("=" * 50)
    app.run(host='0.0.0.0', port=5050, debug=False)
