import time
import os
import json
import sys
from datetime import datetime

def save_internal_state(progress, sensor_data=None):
    """Update progress for the C Manager and Dashboard"""
    state = {
        "progress": progress,
        "task": "Panel Severity Analysis",
        "severity_score": 0,
        "status": "MONITORING",
        "sensor_data": sensor_data or {"T": 0, "D": 0, "L": 0},
        "timestamp": time.time()
    }
    
    # Calculate severity if we have data
    if sensor_data:
        severity = sensor_data.get('T', 30.0) + (20 if sensor_data.get('D', 100) < 10 else 0)
        state["severity_score"] = round(severity, 2)
        state["status"] = "DANGER" if severity > 50 else "CAUTION"

    with open("/tmp/car_detect_internal.json", "w") as f:
        json.dump(state, f)

def run_analysis(start_at=0):
    """The computation loop that moves between Edge and Cloud"""
    print(f"[TASK] Analysis started/resumed at {start_at}%")
    
    for i in range(start_at, 101):
        # 1. ACTUAL CPU LOAD: Force migration trigger
        # We do heavy math for 0.3 seconds to ensure the C manager sees the 'work'
        end_time = time.time() + 0.3
        while time.time() < end_time:
            _ = 5000 * 5000 

        # 2. Get the latest sensor data from the Dashboard's Watchdog
        sensor_data = {"T": 0, "D": 0}
        if os.path.exists("/tmp/sensor_status.json"):
            try:
                with open("/tmp/sensor_status.json", "r") as f:
                    data = json.load(f)
                    sensor_data = data.get("sensor_data", {})
            except: pass

        # 3. Save state (This is what gets migrated!)
        save_internal_state(i, sensor_data)
        
        if i % 10 == 0:
            print(f"[TASK] Current Progress: {i}%")
            
        time.sleep(0.1)

if __name__ == "__main__":
    # Signal that we are alive
    with open("/tmp/dashboard_started", "w") as f:
        f.write("1")

    # Check for migration resume
    start_progress = 0
    if os.path.exists("/tmp/car_detect_internal.json"):
        try:
            with open("/tmp/car_detect_internal.json", "r") as f:
                saved = json.load(f)
                if saved.get("progress", 0) < 100:
                    start_progress = saved["progress"]
        except: pass

    run_analysis(start_progress)
