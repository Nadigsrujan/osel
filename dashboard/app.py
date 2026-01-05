#!/usr/bin/env python3
"""
Edge-Cloud Migration Dashboard - Fixed Version
Real-time monitoring and control panel with actual system telemetry
"""

from flask import Flask, render_template, jsonify, request
import json
import os
import subprocess
import threading
import time
import signal
import sys
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

# Shutdown flag for graceful termination
shutdown_flag = threading.Event()

# State tracking
state = {
    "task_name": None,
    "progress": 0,
    "location": "idle",  # idle, edge, cloud, migrating, completed
    "telemetry": {"cpu": 0, "mem": 0, "temp": None, "battery": None, "network_latency": 0},
    "history": [],
    "edge_pid": None,
    "cloud_pid": None,
    "edge_running": False,
    "cloud_running": False,
    "stress_pids": [],
    "stress_started": False,
    "stress_stopped": False,
    "sensor_status": None,
    "sensor_message": "",
    "sensor_data": {"T": 0, "D": 0, "L": 0}, # Panel Metrics
    # NEW: Decision Engine
    "decision": {
        "reason": "",
        "cpu_forecast": 0,
        "battery_trend": "stable",
        "network_status": "OK",
        "task_criticality": "Normal",
        "action": "None"
    },
    # NEW: Fault/Recovery
    "faults": [],
    "system_health": "OK",
    # NEW: CPU History for graphs (last 30 readings)
    "cpu_history": [],
    "mem_history": [],
    # NEW: Migration Timeline
    "migration_timeline": [],
    # NEW: Task Output
    "task_output": [],
    # NEW: Migration Stats
    "migration_stats": {
        "total_migrations": 0,
        "last_migration_time": None,
        "last_transfer_size": 0,
        "last_downtime_ms": 0
    }
}

def log_event(message):
    """Add event to history with timestamp"""
    event = {
        "time": datetime.now().strftime("%H:%M:%S"),
        "event": message
    }
    state["history"].append(event)
    if len(state["history"]) > 50:
        state["history"] = state["history"][-50:]
    print(f"[{event['time']}] {message}")

def log_timeline(event_type, details):
    """Add to migration timeline"""
    entry = {
        "time": datetime.now().strftime("%H:%M:%S.%f")[:-3],
        "type": event_type,
        "details": details
    }
    state["migration_timeline"].append(entry)
    if len(state["migration_timeline"]) > 100:
        state["migration_timeline"] = state["migration_timeline"][-100:]

def update_decision(reason, action, cpu_forecast=None):
    """Update decision engine state"""
    state["decision"]["reason"] = reason
    state["decision"]["action"] = action
    if cpu_forecast is not None:
        state["decision"]["cpu_forecast"] = cpu_forecast
    state["decision"]["battery_trend"] = "dropping" if state["telemetry"].get("battery", 100) and state["telemetry"]["battery"] < 30 else "stable"
    state["decision"]["network_status"] = "OK" if state["telemetry"].get("network_latency", 0) < 100 else "High Latency"

def log_fault(message):
    """Log a fault/recovery event"""
    fault = {
        "time": datetime.now().strftime("%H:%M:%S"),
        "message": message
    }
    state["faults"].append(fault)
    if len(state["faults"]) > 20:
        state["faults"] = state["faults"][-20:]

def add_task_output(line):
    """Add task output line"""
    state["task_output"].append({
        "time": datetime.now().strftime("%H:%M:%S"),
        "text": line
    })
    if len(state["task_output"]) > 50:
        state["task_output"] = state["task_output"][-50:]

def save_panel_status(status, message, data=None):
    """Write panel sensor status to file for syncing with tasks"""
    status_file = "/tmp/sensor_status.json"
    status_data = {
        "status": status,
        "message": message,
        "timestamp": datetime.now().strftime("%H:%M:%S"),
        "sensor_data": data or {"T": 0, "D": 0, "L": 0}
    }
    try:
        with open(status_file, 'w') as f:
            json.dump(status_data, f)
    except Exception as e:
        print(f"Error saving sensor status: {e}")

def update_cpu_history():
    """Track CPU/Memory history for graphs"""
    state["cpu_history"].append(state["telemetry"]["cpu"])
    state["mem_history"].append(state["telemetry"]["mem"])
    # Keep last 30 readings
    if len(state["cpu_history"]) > 30:
        state["cpu_history"] = state["cpu_history"][-30:]
    if len(state["mem_history"]) > 30:
        state["mem_history"] = state["mem_history"][-30:]

def measure_network_latency():
    """Measure real-world network latency (Edge-to-Cloud link)"""
    import socket
    
    # Target: Real public DNS for demo credibility, or the local cloud manager
    targets = [("8.8.8.8", 53), ("1.1.1.1", 53), ("127.0.0.1", 9090)]
    
    for target_ip, target_port in targets:
        try:
            # Use high-precision performance counter
            start = time.perf_counter()
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.8)
            sock.connect((target_ip, target_port))
            sock.close()
            end = time.perf_counter()
            
            # Actual measured time in milliseconds
            latency = (end - start) * 1000
            
            state["telemetry"]["network_latency"] = round(latency, 2)
            break
        except:
            continue
    
    # Update Decision Engine status based on real measurement
    l = state["telemetry"]["network_latency"]
    if l == 0:
        state["decision"]["network_status"] = "Unknown"
    elif l < 20:
        state["decision"]["network_status"] = "Excellent (Ultra-Low)"
    elif l < 80:
        state["decision"]["network_status"] = "Good (Stable)"
    else:
        state["decision"]["network_status"] = "Congested (High Audit)"


def read_telemetry():
    """Read telemetry - first from C program's JSON, then fallback to psutil"""
    edge_telemetry_file = "/tmp/edge_telemetry.json"
    
    # First try to read from Edge runtime's telemetry file
    if os.path.exists(edge_telemetry_file):
        try:
            with open(edge_telemetry_file, 'r') as f:
                data = json.load(f)
                state["telemetry"]["cpu"] = round(data.get("cpu", 0), 1)
                state["telemetry"]["mem"] = round(data.get("mem", 0), 1)
                state["telemetry"]["temp"] = data.get("temp")
                state["telemetry"]["battery"] = data.get("battery")
                # Also get location and progress from C program
                if "location" in data:
                    state["location"] = data["location"]
                if "progress" in data:
                    state["progress"] = data["progress"]
                return  # Got data from C program
        except Exception as e:
            print(f"Error reading telemetry file: {e}")
    
    # Fallback to psutil if C program not running
    if PSUTIL_AVAILABLE:
        try:
            state["telemetry"]["cpu"] = round(psutil.cpu_percent(interval=0.1), 1)
            mem = psutil.virtual_memory()
            state["telemetry"]["mem"] = round(mem.percent, 1)
            
            try:
                temps = psutil.sensors_temperatures()
                if temps:
                    for name, entries in temps.items():
                        if entries:
                            state["telemetry"]["temp"] = round(entries[0].current, 1)
                            break
            except (AttributeError, Exception):
                state["telemetry"]["temp"] = None
            
            try:
                battery = psutil.sensors_battery()
                if battery:
                    state["telemetry"]["battery"] = round(battery.percent, 1)
                else:
                    state["telemetry"]["battery"] = None
            except (AttributeError, Exception):
                state["telemetry"]["battery"] = None
                
        except Exception as e:
            print(f"Telemetry error: {e}")
    else:
        try:
            with open('/proc/loadavg', 'r') as f:
                load = float(f.read().split()[0])
                cores = os.cpu_count() or 1
                state["telemetry"]["cpu"] = round(min(100, (load / cores) * 100), 1)
        except Exception:
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

def kill_stress_processes():
    """Kill all stress test processes"""
    for pid in state["stress_pids"]:
        try:
            os.kill(pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
    try:
        subprocess.run(["pkill", "-9", "yes"], capture_output=True, timeout=5)
    except subprocess.TimeoutExpired:
        pass
    state["stress_pids"] = []

# Global Serial for Watchdog
ser_watchdog = None

def serial_watchdog_thread():
    """Permanent background thread to listen to Arduino and manage migration triggers"""
    global ser_watchdog
    print("\n[WATCHDOG] 📡 Starting Hardware Watchdog...")
    
    ports = ['/dev/ttyUSB0', '/dev/ttyACM0', '/dev/ttyUSB1', '/dev/cu.usbserial-0001']
    hazard_file = "/tmp/SENSOR_HAZARD"
    
    while not shutdown_flag.is_set():
        if ser_watchdog is None:
            for port in ports:
                try:
                    import serial
                    ser_watchdog = serial.Serial(port, 9600, timeout=1)
                    print(f"[WATCHDOG] ✅ Connected to {port}")
                    log_event(f"🔌 Sensor Hardware Connected: {port}")
                    break
                except Exception as e:
                    continue
            if ser_watchdog is None:
                # Still no port found
                time.sleep(5)
                continue

        try:
            if ser_watchdog.in_waiting > 0:
                line = ser_watchdog.readline().decode('utf-8', errors='ignore').strip()
                
                # Parse metrics for dashboard
                if "T=" in line or "D=" in line:
                    data = {}
                    parts = line.split(',')
                    for p in parts:
                        if '=' in p:
                            try:
                                k, v = p.split('=')
                                data[k] = float(v)
                            except: pass
                    
                    # Update global sensor state
                    state["sensor_data"] = data
                    
                    if "MIGRATE" in line or "CLOUD_DATA" in line:
                        state["sensor_status"] = "detected"
                        state["sensor_message"] = "⚠️ HAZARD DETECTED"
                        # Create physical trigger for C schedulers
                        if not os.path.exists(hazard_file):
                            with open(hazard_file, "w") as f: f.write("1")
                        save_panel_status("detected", "⚠️ HAZARD DETECTED", data)
                    else:
                        state["sensor_status"] = "waiting"
                        state["sensor_message"] = "🟢 System Safe"
                        # Remove trigger
                        if os.path.exists(hazard_file):
                            os.remove(hazard_file)
                        save_panel_status("waiting", "🟢 System Safe", data)

        except Exception as e:
            print(f"[WATCHDOG ERROR] {e}")
            ser_watchdog = None
            time.sleep(2)
        time.sleep(0.05)

# Start the watchdog immediately
threading.Thread(target=serial_watchdog_thread, daemon=True).start()

def read_sensor_status():
    """Now handled by watchdog, but kept for UI sync if needed"""
    pass

def read_task_status():
    """Enhanced task status with migration detection"""
    json_file = "/tmp/car_detect_internal.json"
    edge_telemetry_file = "/tmp/edge_telemetry.json"
    
    # First check process health
    check_process_health()
    
    # Read progress from JSON
    if os.path.exists(json_file):
        try:
            with open(json_file, 'r') as f:
                data = json.load(f)
                old_progress = state["progress"]
                state["progress"] = data.get("progress", 0)
                state["task_name"] = data.get("task", "Panel monitor")
                
                # If sensor data is inside this progress JSON, use it too
                if "sensor_data" in data:
                    state["sensor_data"] = data["sensor_data"]
                
                # If severity score is in JSON, track it
                if "severity_score" in data:
                    state["decision"]["reason"] = f"Severity: {data['severity_score']} ({data.get('status', '')})"
                
                # No more progress milestones or demo stress logic
                pass
                
                # Read raw logs for window
                log_file = "/tmp/task_logs.log"
                if os.path.exists(log_file):
                    with open(log_file, 'r') as lf:
                        lines = lf.readlines()[-10:] # Last 10 lines
                        for line in lines:
                            if line.strip() not in str(state["task_output"]):
                                add_task_output(line.strip())
                
        except Exception as e:
            print(f"Error reading task status: {e}")
    
    # Detect location and migration
    old_location = state["location"]
    
    task_state_exists = os.path.exists("/tmp/task_state.bin")
    edge_return_exists = os.path.exists("/tmp/edge_return.bin")
    cloud_finished_exists = os.path.exists("/tmp/cloud_finished")
    edge_telemetry_exists = os.path.exists(edge_telemetry_file)
    
    # Check if processes are running
    both_stopped = not state["edge_running"] and not state["cloud_running"]
    
    if cloud_finished_exists:
        state["location"] = "completed"
        state["progress"] = 100
        if old_location != "completed":
            log_event("✅ Task Completed on Cloud")
            log_timeline("complete", "Task finished on Cloud")
            update_decision("Task completed successfully", "Complete")
    elif state["progress"] >= 100:
        state["location"] = "completed"
        if old_location != "completed":
            log_event("✅ Task Completed on Edge")
            log_timeline("complete", "Task finished on Edge")
            update_decision("Task completed successfully", "Complete")
    elif task_state_exists and not edge_return_exists:
        state["location"] = "cloud"
        if old_location == "edge":
            log_event("⚡ Migration: Edge → Cloud")
            log_timeline("migrate_to_cloud", f"CPU: {state['telemetry']['cpu']}% - Offloading to Cloud")
            update_decision(
                f"CPU load {state['telemetry']['cpu']}% exceeded threshold (60%)",
                "Migrate to Cloud",
                cpu_forecast=min(100, state['telemetry']['cpu'] + 10)
            )
            state["migration_stats"]["total_migrations"] += 1
            state["migration_stats"]["last_migration_time"] = datetime.now().strftime("%H:%M:%S")
            state["migration_stats"]["last_transfer_size"] = 25  # KB estimate
            state["migration_stats"]["last_downtime_ms"] = 50  # Estimate
            add_task_output(f"[MIGRATE] Checkpointing at {state['progress']}%")
            add_task_output(f"[MIGRATE] Sending state to Cloud...")
    elif edge_return_exists:
        state["location"] = "edge"
        if old_location == "cloud":
            log_event("⚡ Migration: Cloud → Edge")
            log_timeline("migrate_to_edge", f"CPU: {state['telemetry']['cpu']}% - Returning to Edge")
            update_decision(
                f"Edge recovered (CPU {state['telemetry']['cpu']}% < 60%)",
                "Return to Edge",
                cpu_forecast=state['telemetry']['cpu']
            )
            state["migration_stats"]["total_migrations"] += 1
            add_task_output(f"[RETURN] Task resuming on Edge at {state['progress']}%")
            log_fault("✅ System self-healed - task returned to Edge")
    elif state["progress"] > 0:
        state["location"] = "edge"
        update_decision("Running normally on Edge", "Continue", cpu_forecast=state['telemetry']['cpu'])
    elif both_stopped and not edge_telemetry_exists:
        state["location"] = "idle"
        state["progress"] = 0
        state["task_name"] = None
        state["telemetry"] = {"cpu": 0, "mem": 0, "temp": None, "battery": None, "network_latency": 0}
        state["decision"] = {"reason": "", "cpu_forecast": 0, "battery_trend": "stable", "network_status": "OK", "task_criticality": "Normal", "action": "None"}
        state["migration_timeline"] = []
        state["task_output"] = []


def cleanup_files():
    """Clean up state files"""
    files = [
        "/tmp/task_state.bin", 
        "/tmp/edge_return.bin",
        "/tmp/car_detect_internal.json", 
        "/tmp/cloud_finished",
        "/tmp/edge_ready",
        "/tmp/cloud_task_state.bin",
        "/tmp/cloud_return.bin",
        "/tmp/edge_telemetry.json",
        "/tmp/sensor_status.json",
        "/tmp/edge_cpu_status.json"
    ]
    for f in files:
        try:
            if os.path.exists(f):
                os.remove(f)
        except Exception as e:
            print(f"Warning: Could not remove {f}: {e}")

def background_monitor():
    """Background thread to continuously monitor status"""
    while not shutdown_flag.is_set():
        try:
            read_telemetry()
            read_task_status()
            read_sensor_status()
            update_cpu_history()
            measure_network_latency()
            time.sleep(1)
        except Exception as e:
            print(f"Monitor error: {e}")
            time.sleep(5)

def cleanup_on_exit():
    """Clean up resources on exit"""
    print("\n🛑 Shutting down gracefully...")
    shutdown_flag.set()
    
    # Kill all managed processes
    if PSUTIL_AVAILABLE:
        for pid_name in ["edge_pid", "cloud_pid"]:
            pid = state.get(pid_name)
            if pid:
                try:
                    proc = psutil.Process(pid)
                    proc.terminate()
                    proc.wait(timeout=3)
                except Exception:
                    pass
    
    # Force kill if still running
    try:
        subprocess.run(["pkill", "-9", "-f", "edge_runtime"], capture_output=True, timeout=5)
        subprocess.run(["pkill", "-9", "-f", "cloud_runtime"], capture_output=True, timeout=5)
    except subprocess.TimeoutExpired:
        pass
    
    kill_stress_processes()
    cleanup_files()
    print("✅ Cleanup complete")

def signal_handler(signum, frame):
    """Handle shutdown signals"""
    cleanup_on_exit()
    sys.exit(0)

# Register signal handlers
signal.signal(signal.SIGINT, signal_handler)
signal.signal(signal.SIGTERM, signal_handler)

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
        kill_stress_processes()
        
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
        time.sleep(2)  # Increased wait time
        
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
        time.sleep(1)
        if edge_proc.poll() is not None:
            log_event("❌ Edge runtime failed to start")
            # Clean up cloud process
            try:
                cloud_proc.terminate()
            except Exception:
                pass
            return jsonify({"error": "Edge runtime failed to start"}), 500
        
        state["location"] = "edge"
        state["progress"] = 0
        # Reset demo mode flags
        state["stress_started"] = False
        state["stress_stopped"] = False
        state["stress_pids"] = []
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
            for pid_name, label in [("edge_pid", "edge"), ("cloud_pid", "cloud")]:
                pid = state.get(pid_name)
                if pid:
                    try:
                        proc = psutil.Process(pid)
                        proc.terminate()
                        stopped.append(label)
                    except Exception:
                        pass
            
            # Wait for graceful shutdown
            time.sleep(1)
        
        # Force kill if still running
        try:
            subprocess.run(["pkill", "-9", "-f", "edge_runtime"], 
                          capture_output=True, timeout=5)
            subprocess.run(["pkill", "-9", "-f", "cloud_runtime"], 
                          capture_output=True, timeout=5)
            subprocess.run(["pkill", "-9", "-f", "panel_monitor.py"], 
                          capture_output=True, timeout=5)
            subprocess.run(["pkill", "-9", "-f", "car_detect.py"], 
                          capture_output=True, timeout=5)
        except subprocess.TimeoutExpired:
            pass
        
        # Kill stress processes
        kill_stress_processes()
        state["stress_started"] = False
        state["stress_stopped"] = False
        
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
    print("  🚀 Edge-Cloud Migration Dashboard (Fixed)")
    print("=" * 60)
    print(f"  📁 Project Path: {PROJECT_PATH}")
    print(f"  📊 psutil: {'✅ Available' if PSUTIL_AVAILABLE else '❌ Not installed'}")
    print(f"  🌐 Open http://localhost:5050 in your browser")
    print("=" * 60)
    
    if not PSUTIL_AVAILABLE:
        print("\n  ⚠️  For better telemetry, install psutil:")
        print("     pip3 install psutil\n")
    
    try:
        app.run(host='0.0.0.0', port=5050, debug=False, threaded=True)
    finally:
        cleanup_on_exit()