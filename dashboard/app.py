#!/usr/bin/env python3
"""
Edge-Cloud Migration Dashboard - Enhanced Version
Real-time monitoring and control panel with actual system telemetry
"""

from flask import Flask, render_template, jsonify, request
import json
import os
import subprocess
import threading
import time
from datetime import datetime

try:
    import psutil
    PSUTIL_AVAILABLE = True
except ImportError:
    PSUTIL_AVAILABLE = False
    print("[WARNING] psutil not installed. Run: pip3 install psutil")

app = Flask(__name__)

# Dynamic path detection - works on both Mac and Ubuntu
PROJECT_PATH = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLOUD_BIN = os.path.join(PROJECT_PATH, "cloud", "cloud_runtime")
EDGE_BIN = os.path.join(PROJECT_PATH, "edge", "edge_runtime")

# Start time for uptime calculation
start_time = time.time()

# State tracking
state = {
    "task_name": None,
    "progress": 0,
    "location": "idle",  # idle, edge, cloud, migrating, completed
    "telemetry": {"cpu": 0, "mem": 0, "temp": None, "battery": None},
    "history": [],
    "edge_pid": None,
    "cloud_pid": None,
    "edge_running": False,
    "cloud_running": False
}

def log_event(message):
    """Add event to history with timestamp"""
    event = {
        "time": datetime.now().strftime("%H:%M:%S"),
        "event": message
    }
    state["history"].append(event)
    # Keep only last 50 events
    if len(state["history"]) > 50:
        state["history"] = state["history"][-50:]
    print(f"[{event['time']}] {message}")

def read_telemetry():
    """Read real system telemetry using psutil"""
    if PSUTIL_AVAILABLE:
        try:
            # CPU usage
            state["telemetry"]["cpu"] = round(psutil.cpu_percent(interval=0.1), 1)
            
            # Memory usage
            mem = psutil.virtual_memory()
            state["telemetry"]["mem"] = round(mem.percent, 1)
            
            # Temperature (platform-dependent)
            try:
                temps = psutil.sensors_temperatures()
                if temps:
                    for name, entries in temps.items():
                        if entries:
                            state["telemetry"]["temp"] = round(entries[0].current, 1)
                            break
            except:
                state["telemetry"]["temp"] = None
            
            # Battery (if available)
            try:
                battery = psutil.sensors_battery()
                if battery:
                    state["telemetry"]["battery"] = round(battery.percent, 1)
                else:
                    state["telemetry"]["battery"] = None
            except:
                state["telemetry"]["battery"] = None
                
        except Exception as e:
            print(f"Telemetry error: {e}")
    else:
        # Fallback: try reading from /proc (Linux)
        try:
            with open('/proc/loadavg', 'r') as f:
                load = float(f.read().split()[0])
                cores = os.cpu_count() or 1
                state["telemetry"]["cpu"] = round(min(100, (load / cores) * 100), 1)
        except:
            state["telemetry"]["cpu"] = 0

def check_process_health():
    """Check if edge/cloud processes are still alive"""
    if PSUTIL_AVAILABLE:
        # Check edge process
        if state["edge_pid"]:
            try:
                proc = psutil.Process(state["edge_pid"])
                if proc.status() == psutil.STATUS_ZOMBIE:
                    state["edge_pid"] = None
            except psutil.NoSuchProcess:
                state["edge_pid"] = None
        
        # Check cloud process
        if state["cloud_pid"]:
            try:
                proc = psutil.Process(state["cloud_pid"])
                if proc.status() == psutil.STATUS_ZOMBIE:
                    state["cloud_pid"] = None
            except psutil.NoSuchProcess:
                state["cloud_pid"] = None
    
    state["edge_running"] = state["edge_pid"] is not None
    state["cloud_running"] = state["cloud_pid"] is not None

def read_task_status():
    """Enhanced task status with migration detection"""
    json_file = "/tmp/car_detect_internal.json"
    
    # Read progress
    if os.path.exists(json_file):
        try:
            with open(json_file, 'r') as f:
                data = json.load(f)
                old_progress = state["progress"]
                state["progress"] = data.get("progress", 0)
                state["task_name"] = "car_detect"
                
                # Log progress milestones
                for milestone in [25, 50, 75, 100]:
                    if old_progress < milestone <= state["progress"]:
                        log_event(f"📊 Progress: {milestone}%")
        except Exception as e:
            pass
    else:
        if state["progress"] > 0 and state["location"] != "completed":
            state["progress"] = 0
    
    # Detect location and migration
    old_location = state["location"]
    
    task_state_exists = os.path.exists("/tmp/task_state.bin")
    edge_return_exists = os.path.exists("/tmp/edge_return.bin")
    cloud_finished_exists = os.path.exists("/tmp/cloud_finished")
    
    if cloud_finished_exists:
        state["location"] = "completed"
        if old_location != "completed":
            log_event("✅ Task Completed on Cloud")
    elif task_state_exists and not edge_return_exists:
        state["location"] = "cloud"
        if old_location == "edge":
            log_event("⚡ Migration: Edge → Cloud")
    elif edge_return_exists:
        state["location"] = "edge"
        if old_location == "cloud":
            log_event("⚡ Migration: Cloud → Edge")
    elif state["progress"] >= 100:
        state["location"] = "completed"
        if old_location != "completed":
            log_event("✅ Task Completed on Edge")
    elif state["progress"] > 0:
        state["location"] = "edge"
    elif not state["edge_running"] and not state["cloud_running"]:
        state["location"] = "idle"
    
    check_process_health()

def cleanup_files():
    """Clean up state files"""
    files = [
        "/tmp/task_state.bin", 
        "/tmp/edge_return.bin",
        "/tmp/car_detect_internal.json", 
        "/tmp/cloud_finished",
        "/tmp/edge_ready",
        "/tmp/cloud_task_state.bin",
        "/tmp/cloud_return.bin"
    ]
    for f in files:
        try:
            if os.path.exists(f):
                os.remove(f)
        except Exception as e:
            print(f"Warning: Could not remove {f}: {e}")

def background_monitor():
    """Background thread to continuously monitor status"""
    while True:
        try:
            read_telemetry()
            read_task_status()
            time.sleep(1)
        except Exception as e:
            print(f"Monitor error: {e}")
            time.sleep(5)

# Start background monitoring thread
monitor_thread = threading.Thread(target=background_monitor, daemon=True)
monitor_thread.start()

@app.route('/')
def index():
    return render_template('dashboard.html')

@app.route('/api/status')
def api_status():
    """Return comprehensive system status"""
    return jsonify({
        **state,
        "timestamp": datetime.now().isoformat(),
        "uptime": round(time.time() - start_time, 1),
        "psutil_available": PSUTIL_AVAILABLE,
        "project_path": PROJECT_PATH,
        "files_exist": {
            "task_state": os.path.exists("/tmp/task_state.bin"),
            "edge_return": os.path.exists("/tmp/edge_return.bin"),
            "status_json": os.path.exists("/tmp/car_detect_internal.json"),
            "cloud_finished": os.path.exists("/tmp/cloud_finished")
        }
    })

@app.route('/api/start', methods=['POST'])
def api_start():
    """Start a new task with proper initialization"""
    try:
        # Clean up old state
        cleanup_files()
        
        # Check if binaries exist
        if not os.path.exists(CLOUD_BIN):
            log_event(f"❌ Cloud binary not found")
            return jsonify({"error": f"Cloud binary not found: {CLOUD_BIN}"}), 400
        if not os.path.exists(EDGE_BIN):
            log_event(f"❌ Edge binary not found")
            return jsonify({"error": f"Edge binary not found: {EDGE_BIN}"}), 400
        
        # Start cloud runtime
        log_event("🚀 Starting Cloud Runtime...")
        cloud_proc = subprocess.Popen(
            [CLOUD_BIN],
            cwd=PROJECT_PATH,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        state["cloud_pid"] = cloud_proc.pid
        time.sleep(1.5)
        
        # Verify cloud started
        if cloud_proc.poll() is not None:
            log_event("❌ Cloud runtime failed to start")
            return jsonify({"error": "Cloud runtime failed to start"}), 500
        
        # Start edge runtime
        log_event("🚀 Starting Edge Runtime...")
        edge_proc = subprocess.Popen(
            [EDGE_BIN],
            cwd=PROJECT_PATH,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        state["edge_pid"] = edge_proc.pid
        
        # Verify edge started
        time.sleep(0.5)
        if edge_proc.poll() is not None:
            log_event("❌ Edge runtime failed to start")
            return jsonify({"error": "Edge runtime failed to start"}), 500
        
        state["location"] = "edge"
        state["progress"] = 0
        log_event("✅ Task started on Edge")
        
        return jsonify({
            "status": "started",
            "edge_pid": state["edge_pid"],
            "cloud_pid": state["cloud_pid"]
        })
        
    except Exception as e:
        log_event(f"❌ Start failed: {str(e)}")
        return jsonify({"error": str(e)}), 500

@app.route('/api/stop', methods=['POST'])
def api_stop():
    """Stop all tasks gracefully"""
    try:
        stopped = []
        
        if PSUTIL_AVAILABLE:
            # Try graceful shutdown first (SIGTERM)
            if state["edge_pid"]:
                try:
                    proc = psutil.Process(state["edge_pid"])
                    proc.terminate()
                    stopped.append("edge")
                except:
                    pass
            
            if state["cloud_pid"]:
                try:
                    proc = psutil.Process(state["cloud_pid"])
                    proc.terminate()
                    stopped.append("cloud")
                except:
                    pass
            
            # Wait for graceful shutdown
            time.sleep(1)
        
        # Force kill if still running
        subprocess.run(["pkill", "-9", "-f", "edge_runtime"], 
                      capture_output=True, timeout=5)
        subprocess.run(["pkill", "-9", "-f", "cloud_runtime"], 
                      capture_output=True, timeout=5)
        subprocess.run(["pkill", "-9", "-f", "car_detect.py"], 
                      capture_output=True, timeout=5)
        
        cleanup_files()
        
        state["location"] = "idle"
        state["progress"] = 0
        state["edge_pid"] = None
        state["cloud_pid"] = None
        state["task_name"] = None
        
        log_event(f"🛑 Stopped all processes")
        
        return jsonify({"status": "stopped", "processes": stopped})
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/api/history')
def api_history():
    return jsonify(state["history"][-30:])

@app.route('/api/health')
def api_health():
    """Health check endpoint"""
    return jsonify({
        "status": "ok",
        "uptime": round(time.time() - start_time, 1),
        "psutil": PSUTIL_AVAILABLE,
        "binaries": {
            "edge": os.path.exists(EDGE_BIN),
            "cloud": os.path.exists(CLOUD_BIN)
        }
    })

if __name__ == '__main__':
    print("=" * 60)
    print("  🚀 Edge-Cloud Migration Dashboard (Enhanced)")
    print("=" * 60)
    print(f"  📁 Project Path: {PROJECT_PATH}")
    print(f"  📊 psutil: {'✅ Available' if PSUTIL_AVAILABLE else '❌ Not installed'}")
    print(f"  🌐 Open http://localhost:5050 in your browser")
    print("=" * 60)
    
    if not PSUTIL_AVAILABLE:
        print("\n  ⚠️  For better telemetry, install psutil:")
        print("     pip3 install psutil\n")
    
    app.run(host='0.0.0.0', port=5050, debug=False, threaded=True)
